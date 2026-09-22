// @ai-generated(solo)
#include <stdlib.h>
#include <string.h>

#include "clj/box.h"
#include "clj/chan.h"
#include "clj/cmutex.h"
#include "clj/core.h"
#include "clj/coro.h"
#include "clj/error.h"
#include "clj/eval.h"
#include "clj/fn.h"
#include "clj/keyword.h"
#include "clj/lock.h"
#include "clj/map.h"
#include "clj/ns.h"
#include "clj/runtime.h"
#include "clj/string.h"
#include "clj/symbol.h"
#include "clj/var.h"
#include "clj/vector.h"
#include "coro_internal.h"

// ---- the buffer spec

typedef struct {
	clj_header h;
	uint8_t    kind;
	uint32_t   cap;
} clj_buffer;

static uint32_t identity_hash(void *self) { return clj_fmix32((uint32_t)((uintptr_t)self >> 4)); }

static bool identity_equals(void *self, clj_value other) { return clj_from_ptr(self) == other; }

const clj_type clj_buffer_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "buffer",
	.hash = identity_hash,
	.equals = identity_equals,
};

clj_value clj_buffer_new(int kind, uint32_t cap) {
	clj_buffer *b = clj_alloc(&clj_buffer_type, sizeof *b);
	b->kind = (uint8_t)kind;
	b->cap = cap;
	return clj_from_ptr(b);
}

// ---- the channel

// A queue entry: a waiter (shared across the ports of an alts!), the value a putter offers, the port's index.
typedef struct qnode {
	clj_waiter   *w; // NULL for a fire-and-forget put (the go block's result)
	clj_value     value;
	uint32_t      index;
	struct qnode *next;
} qnode;

typedef struct thread_job thread_job;

typedef struct {
	clj_header h;
	clj_lock   lock;      // a plain channel: a runtime-only section
	clj_cmutex cm;        // a channel with a transducer: its step runs user code, which may park
	clj_coro  *cm_owner;  // the execution inside the section of an xform channel, for the reentry trap
	uint8_t    kind;
	uint8_t    role;
	bool       closed;
	bool       completed; // the transducer's completion arity ran
	uint32_t   cap, count, head, ring_cap;
	clj_value *ring;
	qnode     *takers, *takers_tail;
	qnode     *putters, *putters_tail;
	uint32_t   ntakers, nputters;
	clj_value  coro;       // the coroutine feeding the channel, for cancel!; nil otherwise
	thread_job *job;       // a thread's job before its coroutine is attached, for a cancel! that comes first
	clj_value  add_fn;     // (xform rf), nil without a transducer
	clj_value  ex_handler; // fn or nil
	clj_value  error;      // a future whose body threw: the value deref rethrows
} clj_chan;

static clj_chan *chan_of(clj_value v) { return (clj_chan *)clj_to_ptr(v); }

static bool has_xform(const clj_chan *ch) { return !clj_is_nil(ch->add_fn); }

static void chan_each_child(void *self, clj_visitor visit, void *ctx) {
	clj_chan *ch = self;
	for (uint32_t i = 0; i < ch->count; i++) visit(ch->ring[(ch->head + i) % ch->ring_cap], ctx);
	for (qnode *n = ch->putters; n; n = n->next) visit(n->value, ctx);
	visit(ch->coro, ctx);
	visit(ch->add_fn, ctx);
	visit(ch->ex_handler, ctx);
	visit(ch->error, ctx);
}

static void free_nodes(qnode *n) {
	while (n) {
		qnode *next = n->next;
		if (n->w) clj_waiter_release(n->w);
		free(n);
		n = next;
	}
}

// The values were released through each_child; the nodes and the waiters' own references remain.
static void chan_finalize(void *self) {
	clj_chan *ch = self;
	free(ch->ring);
	free_nodes(ch->takers);
	free_nodes(ch->putters);
	clj_lock_destroy(&ch->lock);
}

const clj_type clj_chan_type = {
	.h = {1, CLJ_FLAG_IMMORTAL, &clj_type_type},
	.name = "channel",
	.each_child = chan_each_child,
	.finalize = chan_finalize,
	.hash = identity_hash,
	.equals = identity_equals,
};

static clj_value chan_alloc(int kind, uint32_t cap, int role) {
	clj_chan *ch = clj_alloc(&clj_chan_type, sizeof *ch);
	memset((char *)ch + sizeof ch->h, 0, sizeof *ch - sizeof ch->h);
	clj_lock_init(&ch->lock);
	clj_cmutex_init(&ch->cm);
	ch->kind = (uint8_t)kind;
	ch->role = (uint8_t)role;
	ch->cap = cap;
	ch->ring_cap = cap;
	if (cap) {
		ch->ring = malloc(cap * sizeof *ch->ring);
		if (!ch->ring) clj_fatal("out of memory");
	}
	ch->coro = ch->add_fn = ch->ex_handler = ch->error = CLJ_NIL;
	return clj_from_ptr(ch);
}

static clj_value buffer_spec(clj_value buf_or_n, int *kind, uint32_t *cap) {
	*kind = CLJ_BUF_NONE;
	*cap = 0;
	if (clj_is_buffer(buf_or_n)) {
		*kind = ((clj_buffer *)clj_to_ptr(buf_or_n))->kind;
		*cap = ((clj_buffer *)clj_to_ptr(buf_or_n))->cap;
	} else if (clj_is_fixnum(buf_or_n)) {
		if (clj_fixnum_val(buf_or_n) < 0) return clj_throw_msg("chan: a buffer size must not be negative");
		*cap = (uint32_t)clj_fixnum_val(buf_or_n);
		*kind = *cap ? CLJ_BUF_FIXED : CLJ_BUF_NONE;
	} else if (!clj_is_nil(buf_or_n)) {
		return clj_throw_msg("chan expects a buffer or a size, got: %s", clj_type_name(buf_or_n));
	}
	if (*kind != CLJ_BUF_NONE && *cap == 0) return clj_throw_msg("chan: a buffer must have a positive size");
	return CLJ_NIL;
}

clj_value clj_chan_new(clj_value buf_or_n) {
	int      kind;
	uint32_t cap;
	if (buffer_spec(buf_or_n, &kind, &cap) == CLJ_THROWN) return CLJ_THROWN;
	return chan_alloc(kind, cap, CLJ_CHAN_PLAIN);
}

// The reducing fn the transducer wraps: the accumulator is the channel itself, so no reference cycle is made.
static void buffer_add(clj_chan *ch, clj_value v);

static clj_value b_chan_rf(const clj_value *args, size_t n) {
	if (!clj_is_chan(args[0])) return clj_throw_msg("a channel's reducing fn expects the channel, got: %s", clj_type_name(args[0]));
	if (n == 2) {
		clj_chan *ch = chan_of(args[0]);
		if (ch->cm_owner != clj_coro_current()) return clj_throw_msg("a channel's reducing fn was called outside its transducer step");
		clj_share(args[1]);
		buffer_add(ch, clj_retain(args[1]));
	}
	return clj_retain(args[0]);
}

static clj_value chan_rf;

clj_value clj_chan_new_xform(clj_value buf_or_n, clj_value xform, clj_value ex_handler) {
	if (clj_is_nil(xform)) return clj_chan_new(buf_or_n);
	int      kind;
	uint32_t cap;
	if (buffer_spec(buf_or_n, &kind, &cap) == CLJ_THROWN) return CLJ_THROWN;
	if (kind == CLJ_BUF_NONE) return clj_throw_msg("chan: buffer must be supplied when transducer is");
	if (!clj_has_core(xform, CLJ_CORE_FN)) return clj_throw_msg("chan expects a transducer fn, got: %s", clj_type_name(xform));
	if (!clj_is_nil(ex_handler) && !clj_has_core(ex_handler, CLJ_CORE_FN)) return clj_throw_msg("chan expects an ex-handler fn, got: %s", clj_type_name(ex_handler));
	clj_value add_fn = clj_invoke(xform, &chan_rf, 1);
	if (add_fn == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_has_core(add_fn, CLJ_CORE_FN)) {
		clj_release(add_fn);
		return clj_throw_msg("chan: the transducer did not return a fn");
	}
	clj_value chv = chan_alloc(kind, cap, CLJ_CHAN_PLAIN);
	clj_chan *ch = chan_of(chv);
	clj_share(add_fn);
	clj_share(ex_handler);
	ch->add_fn = add_fn;
	ch->ex_handler = clj_retain(ex_handler);
	return chv;
}

// ---- the section: a clj_lock for a plain channel, the coroutine mutex when a transducer step runs inside

static void chan_lock(clj_chan *ch) {
	if (has_xform(ch)) {
		clj_cmutex_lock(&ch->cm);
		ch->cm_owner = clj_coro_current();
	} else {
		clj_lock_lock(&ch->lock);
	}
}

static void chan_unlock(clj_chan *ch) {
	if (has_xform(ch)) {
		ch->cm_owner = NULL;
		clj_cmutex_unlock(&ch->cm);
	} else {
		clj_lock_unlock(&ch->lock);
	}
}

// The mutex is not reentrant: a step touching its own channel is refused, not deadlocked.
static bool reentry(clj_chan *ch, const char *op) {
	if (has_xform(ch) && ch->cm_owner == clj_coro_current()) {
		clj_throw_msg("%s on a channel from inside its own transducer step", op);
		return true;
	}
	return false;
}

// ---- queues

static qnode *node_new(clj_waiter *w, clj_value value, uint32_t index) {
	qnode *n = malloc(sizeof *n);
	if (!n) clj_fatal("out of memory");
	if (w) clj_waiter_retain(w);
	n->w = w;
	n->value = value;
	n->index = index;
	n->next = NULL;
	return n;
}

static void enqueue(qnode **head, qnode **tail, qnode *n) {
	if (*tail) (*tail)->next = n;
	else *head = n;
	*tail = n;
}

static qnode *dequeue(qnode **head, qnode **tail) {
	qnode *n = *head;
	if (!n) return NULL;
	*head = n->next;
	if (!*head) *tail = NULL;
	n->next = NULL;
	return n;
}

static void node_drop(qnode *n) {
	if (n->w) clj_waiter_release(n->w);
	clj_release(n->value);
	free(n);
}

// Claimed by the caller's side: 0 the pair is claimed (n is live), 1 the actor was claimed elsewhere, 2 stale n.
static int claim_node(clj_waiter *actor, qnode *n) { return clj_waiter_claim_pair(actor, n->w); }

// The first entry claimed together with the actor; NULL with *actor_stale when the actor won elsewhere.
static qnode *pop_live(qnode **head, qnode **tail, uint32_t *count, clj_waiter *actor, bool *actor_stale) {
	*actor_stale = false;
	for (;;) {
		qnode *n = *head;
		if (!n) return NULL;
		int r = claim_node(actor, n);
		if (r == 1) {
			*actor_stale = true;
			return NULL;
		}
		dequeue(head, tail);
		(*count)--;
		if (r == 0) return n;
		node_drop(n);
	}
}

static void purge_stale(qnode **head, qnode **tail, uint32_t *count) {
	qnode **at = head, *prev = NULL;
	while (*at) {
		qnode *n = *at;
		if (n->w && atomic_load_explicit(&n->w->claimed, memory_order_acquire)) {
			*at = n->next;
			if (*tail == n) *tail = prev;
			(*count)--;
			node_drop(n);
			continue;
		}
		prev = n;
		at = &n->next;
	}
}

// Resumes a woken waiter after the lock: value and ok are what its operation yields, port the channel it was on.
static void wake(qnode *n, clj_value value, bool ok, clj_value port) {
	if (n->w) {
		clj_waiter *w = n->w;
		clj_release(w->value);
		w->value = clj_retain(value);
		w->ok = ok;
		clj_release(w->port);
		w->port = clj_retain(port);
		w->index = n->index;
		clj_resume(w);
		clj_waiter_release(w);
	}
	clj_release(n->value);
	free(n);
}

// ---- the buffer

static bool buffer_full(const clj_chan *ch) { return ch->kind == CLJ_BUF_FIXED && ch->count >= ch->cap; }

static bool buffer_has_room(const clj_chan *ch) { return ch->kind != CLJ_BUF_NONE && !buffer_full(ch); }

// A transducer may expand one input into many: a fixed ring grows past its capacity (the JVM's is a list).
static void ring_grow(clj_chan *ch) {
	uint32_t   cap = ch->ring_cap * 2;
	clj_value *ring = malloc(cap * sizeof *ring);
	if (!ring) clj_fatal("out of memory");
	for (uint32_t i = 0; i < ch->count; i++) ring[i] = ch->ring[(ch->head + i) % ch->ring_cap];
	free(ch->ring);
	ch->ring = ring;
	ch->ring_cap = cap;
	ch->head = 0;
}

// Takes the value (+1); a full dropping buffer drops it, a full sliding one its oldest, a promise buffer its second.
static void buffer_add(clj_chan *ch, clj_value v) {
	if (ch->kind == CLJ_BUF_PROMISE) {
		if (ch->count) {
			clj_release(v);
			return;
		}
	} else if (ch->count == ch->cap && ch->kind != CLJ_BUF_FIXED) {
		if (ch->kind == CLJ_BUF_DROPPING) {
			clj_release(v);
			return;
		}
		clj_value oldest = ch->ring[ch->head];
		ch->head = (ch->head + 1) % ch->ring_cap;
		ch->count--;
		clj_release(oldest);
	} else if (ch->count == ch->ring_cap) {
		ring_grow(ch);
	}
	ch->ring[(ch->head + ch->count) % ch->ring_cap] = v;
	ch->count++;
}

// Owned; a promise buffer's value stays.
static clj_value buffer_take(clj_chan *ch) {
	if (ch->kind == CLJ_BUF_PROMISE) return clj_retain(ch->ring[0]);
	clj_value v = ch->ring[ch->head];
	ch->head = (ch->head + 1) % ch->ring_cap;
	ch->count--;
	return v;
}

// ---- the operations under the lock; woken nodes go to a list the caller flushes after unlocking

typedef struct {
	qnode    *nodes[16];
	size_t    n;
	clj_value port;
} wakes;

static void add_wake(wakes *ws, qnode *n, clj_value value, bool ok) {
	if (ws->n < 16) {
		clj_release(n->value);
		n->value = clj_retain(value);
		ws->nodes[ws->n++] = n;
		return;
	}
	// A close! with more than 16 parked takers wakes the rest under the lock: still no user code there.
	wake(n, value, ok, ws->port);
}

static void flush_wakes(wakes *ws, bool ok) {
	for (size_t i = 0; i < ws->n; i++) {
		qnode    *n = ws->nodes[i];
		clj_value v = n->value;
		n->value = CLJ_NIL;
		wake(n, v, ok, ws->port);
		clj_release(v);
	}
	ws->n = 0;
}

enum { OP_DONE, OP_NOT_READY, OP_STALE };

// Every live taker gets the buffer's head while there is one (a promise buffer serves all of them).
static void drain_to_takers(clj_chan *ch, clj_value chv, wakes *ws) {
	while (ch->count) {
		bool   stale;
		qnode *t = pop_live(&ch->takers, &ch->takers_tail, &ch->ntakers, NULL, &stale);
		if (!t) return;
		clj_value v = buffer_take(ch);
		ws->port = chv;
		add_wake(ws, t, v, true);
		clj_release(v);
	}
}

// ---- the transducer step, under the coroutine mutex: user code, exceptions to the ex-handler

// The pending exception goes to the ex-handler; a non-nil answer is added instead (the JVM's contract).
static void step_failed(clj_chan *ch) {
	clj_coro *c = clj_coro_current();
	if (clj_is_nil(ch->ex_handler)) {
		clj_coro_report_uncaught(c);
		clj_coro_drop_pending(c);
		return;
	}
	clj_value ex = clj_take_pending();
	clj_value r = clj_invoke(ch->ex_handler, &ex, 1);
	clj_release(ex);
	if (r == CLJ_THROWN) {
		clj_coro_report_uncaught(c);
		clj_coro_drop_pending(c);
		return;
	}
	if (clj_is_nil(r)) return;
	clj_share(r);
	buffer_add(ch, r);
}

// True when the transducer said reduced: the channel is then aborted.
static bool step_add(clj_chan *ch, clj_value chv, clj_value v) {
	clj_value args[2] = {chv, v};
	clj_value r = clj_invoke(ch->add_fn, args, 2);
	if (r == CLJ_THROWN) {
		step_failed(ch);
		return false;
	}
	bool done = clj_is_reduced(r);
	clj_release(r);
	return done;
}

static void step_complete(clj_chan *ch, clj_value chv) {
	if (ch->completed) return;
	ch->completed = true;
	clj_value r = clj_invoke(ch->add_fn, &chv, 1);
	if (r == CLJ_THROWN) step_failed(ch);
	else clj_release(r);
}

// The reduced answer: the channel closes and every parked put completes without a transfer.
static void abort_locked(clj_chan *ch, clj_value chv, wakes *ws) {
	ch->closed = true;
	for (;;) {
		bool   stale;
		qnode *p = pop_live(&ch->putters, &ch->putters_tail, &ch->nputters, NULL, &stale);
		if (!p) break;
		ws->port = chv;
		add_wake(ws, p, CLJ_TRUE, true);
	}
}

static void close_locked(clj_chan *ch, clj_value chv, wakes *ws);

// The actor is claimed together with its counterparty; OP_DONE with *ok false means closed.
static int put_locked(clj_chan *ch, clj_value chv, clj_value v, clj_waiter *actor, bool *ok, wakes *ws) {
	if (ch->closed) {
		if (actor && !clj_waiter_claim(actor)) return OP_STALE;
		*ok = false;
		return OP_DONE;
	}
	if (has_xform(ch)) {
		if (!buffer_has_room(ch)) return OP_NOT_READY;
		if (actor && !clj_waiter_claim(actor)) return OP_STALE;
		bool done = step_add(ch, chv, v);
		drain_to_takers(ch, chv, ws);
		if (done) {
			abort_locked(ch, chv, ws);
			close_locked(ch, chv, ws);
		}
		*ok = true;
		return OP_DONE;
	}
	if (ch->kind == CLJ_BUF_PROMISE) {
		if (actor && !clj_waiter_claim(actor)) return OP_STALE;
		buffer_add(ch, clj_retain(v));
		drain_to_takers(ch, chv, ws);
		*ok = true;
		return OP_DONE;
	}
	bool   stale;
	qnode *t = pop_live(&ch->takers, &ch->takers_tail, &ch->ntakers, actor, &stale);
	if (stale) return OP_STALE;
	if (t) {
		ws->port = chv;
		add_wake(ws, t, v, true);
		*ok = true;
		return OP_DONE;
	}
	if (buffer_has_room(ch)) {
		if (actor && !clj_waiter_claim(actor)) return OP_STALE;
		buffer_add(ch, clj_retain(v));
		*ok = true;
		return OP_DONE;
	}
	return OP_NOT_READY;
}

// Parked putters refill a buffer that made room; with a transducer each goes through the step.
static void refill(clj_chan *ch, clj_value chv, wakes *ws) {
	while (!buffer_full(ch)) {
		bool   stale;
		qnode *p = pop_live(&ch->putters, &ch->putters_tail, &ch->nputters, NULL, &stale);
		if (!p) return;
		bool done = false;
		if (has_xform(ch)) {
			done = step_add(ch, chv, p->value);
			clj_release(p->value);
		} else {
			buffer_add(ch, p->value);
		}
		p->value = CLJ_NIL;
		ws->port = chv;
		add_wake(ws, p, CLJ_TRUE, true);
		if (done) {
			abort_locked(ch, chv, ws);
			close_locked(ch, chv, ws);
			return;
		}
	}
}

static int take_locked(clj_chan *ch, clj_value chv, clj_waiter *actor, clj_value *out, wakes *ws) {
	if (ch->count) {
		if (actor && !clj_waiter_claim(actor)) return OP_STALE;
		*out = buffer_take(ch);
		if (ch->kind != CLJ_BUF_PROMISE) refill(ch, chv, ws);
		return OP_DONE;
	}
	if (!has_xform(ch)) {
		bool   stale;
		qnode *p = pop_live(&ch->putters, &ch->putters_tail, &ch->nputters, actor, &stale);
		if (stale) return OP_STALE;
		if (p) {
			*out = p->value;
			p->value = CLJ_NIL;
			ws->port = chv;
			add_wake(ws, p, CLJ_TRUE, true);
			return OP_DONE;
		}
	}
	if (ch->closed) {
		if (has_xform(ch)) {
			step_complete(ch, chv);
			if (ch->count) {
				if (actor && !clj_waiter_claim(actor)) return OP_STALE;
				*out = buffer_take(ch);
				return OP_DONE;
			}
		}
		if (actor && !clj_waiter_claim(actor)) return OP_STALE;
		*out = CLJ_NIL;
		return OP_DONE;
	}
	return OP_NOT_READY;
}

static bool pending_full(qnode **head, qnode **tail, uint32_t *count) {
	if (*count < CLJ_CHAN_MAX_PENDING) return false;
	purge_stale(head, tail, count);
	return *count >= CLJ_CHAN_MAX_PENDING;
}

// ---- <! and >!

static bool cancelled_here(void) {
	clj_coro *c = clj_coro_current();
	return c->shadow && atomic_load_explicit(&c->shadow->cancelled, memory_order_relaxed);
}

static clj_value cancelled_throw(void) { return clj_throw_cancelled(clj_coro_cancel_is_deadline(clj_coro_current())); }

static clj_value chan_arg(clj_value ch, const char *op) {
	if (!clj_is_chan(ch)) return clj_throw_msg("%s expects a channel, got: %s", op, clj_type_name(ch));
	if (reentry(chan_of(ch), op)) return CLJ_THROWN;
	return CLJ_NIL;
}

static clj_value pending_error(bool puts) {
	return clj_throw_msg("No more than %d pending %s are allowed on a single channel.", CLJ_CHAN_MAX_PENDING, puts ? "puts" : "takes");
}

clj_value clj_chan_put(clj_value chv, clj_value v) {
	if (chan_arg(chv, ">!") == CLJ_THROWN) return CLJ_THROWN;
	if (clj_is_nil(v)) return clj_throw_msg("Can't put nil on channel");
	if (!clj_park_allowed()) return CLJ_THROWN;
	clj_chan *ch = chan_of(chv);
	clj_share(v);
	wakes       ws = {.n = 0};
	bool        ok = false;
	clj_waiter *w = NULL;
	chan_lock(ch);
	int r = put_locked(ch, chv, v, NULL, &ok, &ws);
	if (r == OP_NOT_READY) {
		if (pending_full(&ch->putters, &ch->putters_tail, &ch->nputters)) {
			chan_unlock(ch);
			return pending_error(true);
		}
		w = clj_waiter_new(clj_coro_current(), CLJ_NIL);
		enqueue(&ch->putters, &ch->putters_tail, node_new(w, clj_retain(v), 0));
		ch->nputters++;
	}
	chan_unlock(ch);
	flush_wakes(&ws, true);
	if (!w) return clj_bool(ok);
	clj_park(w);
	ok = w->ok;
	clj_waiter_release(w);
	if (cancelled_here()) return cancelled_throw();
	return clj_bool(ok);
}

// uncancellable: a scope's join, which must outlast its own cancellation.
static clj_value chan_take(clj_value chv, bool uncancellable) {
	if (chan_arg(chv, "<!") == CLJ_THROWN) return CLJ_THROWN;
	if (!uncancellable && !clj_park_allowed()) return CLJ_THROWN;
	clj_chan   *ch = chan_of(chv);
	wakes       ws = {.n = 0};
	clj_value   out = CLJ_NIL;
	clj_waiter *w = NULL;
	chan_lock(ch);
	int r = take_locked(ch, chv, NULL, &out, &ws);
	if (r == OP_NOT_READY) {
		if (pending_full(&ch->takers, &ch->takers_tail, &ch->ntakers)) {
			chan_unlock(ch);
			return pending_error(false);
		}
		w = clj_waiter_new(clj_coro_current(), CLJ_NIL);
		enqueue(&ch->takers, &ch->takers_tail, node_new(w, CLJ_NIL, 0));
		ch->ntakers++;
	}
	chan_unlock(ch);
	flush_wakes(&ws, true);
	if (!w) return out;
	if (uncancellable) clj_park_uncancellable(w);
	else clj_park(w);
	out = w->value;
	w->value = CLJ_NIL;
	clj_waiter_release(w);
	if (!uncancellable && cancelled_here()) {
		clj_release(out);
		return cancelled_throw();
	}
	return out;
}

clj_value clj_chan_take(clj_value chv) { return chan_take(chv, false); }

clj_value clj_chan_offer(clj_value chv, clj_value v) {
	if (chan_arg(chv, "offer!") == CLJ_THROWN) return CLJ_THROWN;
	if (clj_is_nil(v)) return clj_throw_msg("Can't put nil on channel");
	clj_chan *ch = chan_of(chv);
	clj_share(v);
	wakes ws = {.n = 0};
	bool  ok = false;
	chan_lock(ch);
	int r = put_locked(ch, chv, v, NULL, &ok, &ws);
	chan_unlock(ch);
	flush_wakes(&ws, true);
	return r == OP_DONE ? clj_bool(ok) : CLJ_NIL;
}

clj_value clj_chan_poll(clj_value chv) {
	if (chan_arg(chv, "poll!") == CLJ_THROWN) return CLJ_THROWN;
	clj_chan *ch = chan_of(chv);
	wakes     ws = {.n = 0};
	clj_value out = CLJ_NIL;
	chan_lock(ch);
	take_locked(ch, chv, NULL, &out, &ws);
	chan_unlock(ch);
	flush_wakes(&ws, true);
	return out;
}

// ---- put! and take! with callbacks

static void callback_now(clj_value fn, clj_value v, bool on_caller) {
	if (clj_is_nil(fn)) return;
	if (on_caller) {
		clj_value r = clj_invoke(fn, &v, 1);
		if (r == CLJ_THROWN) {
			clj_coro *c = clj_coro_current();
			clj_coro_report_uncaught(c);
			clj_coro_drop_pending(c);
		} else {
			clj_release(r);
		}
		return;
	}
	clj_value coro = clj_coro_spawn(fn, &v, 1, CLJ_AFFINITY_POOL, NULL, NULL);
	clj_release(coro);
}

clj_value clj_chan_put_cb(clj_value chv, clj_value v, clj_value fn, bool on_caller) {
	if (chan_arg(chv, "put!") == CLJ_THROWN) return CLJ_THROWN;
	if (clj_is_nil(v)) return clj_throw_msg("Can't put nil on channel");
	if (!clj_is_nil(fn) && !clj_has_core(fn, CLJ_CORE_FN)) return clj_throw_msg("put! expects a fn callback, got: %s", clj_type_name(fn));
	clj_chan *ch = chan_of(chv);
	clj_share(v);
	clj_share(fn);
	wakes ws = {.n = 0};
	bool  ok = false;
	chan_lock(ch);
	int r = put_locked(ch, chv, v, NULL, &ok, &ws);
	if (r == OP_NOT_READY) {
		if (pending_full(&ch->putters, &ch->putters_tail, &ch->nputters)) {
			chan_unlock(ch);
			return pending_error(true);
		}
		clj_waiter *w = clj_waiter_new(NULL, fn);
		enqueue(&ch->putters, &ch->putters_tail, node_new(w, clj_retain(v), 0));
		ch->nputters++;
		clj_waiter_release(w);
		chan_unlock(ch);
		return CLJ_TRUE;
	}
	chan_unlock(ch);
	flush_wakes(&ws, true);
	callback_now(fn, clj_bool(ok), on_caller);
	return clj_bool(ok);
}

clj_value clj_chan_take_cb(clj_value chv, clj_value fn, bool on_caller) {
	if (chan_arg(chv, "take!") == CLJ_THROWN) return CLJ_THROWN;
	if (!clj_has_core(fn, CLJ_CORE_FN)) return clj_throw_msg("take! expects a fn callback, got: %s", clj_type_name(fn));
	clj_chan *ch = chan_of(chv);
	clj_share(fn);
	wakes     ws = {.n = 0};
	clj_value out = CLJ_NIL;
	chan_lock(ch);
	int r = take_locked(ch, chv, NULL, &out, &ws);
	if (r == OP_NOT_READY) {
		if (pending_full(&ch->takers, &ch->takers_tail, &ch->ntakers)) {
			chan_unlock(ch);
			return pending_error(false);
		}
		clj_waiter *w = clj_waiter_new(NULL, fn);
		enqueue(&ch->takers, &ch->takers_tail, node_new(w, CLJ_NIL, 0));
		ch->ntakers++;
		clj_waiter_release(w);
		chan_unlock(ch);
		return CLJ_NIL;
	}
	chan_unlock(ch);
	flush_wakes(&ws, true);
	callback_now(fn, out, on_caller);
	clj_release(out);
	return CLJ_NIL;
}

// ---- close!

// Parked puts stay (their values are still taken); takers get the completion's flush, the promise value or nil.
static void close_locked(clj_chan *ch, clj_value chv, wakes *ws) {
	if (ch->closed && !(has_xform(ch) && !ch->completed && !ch->putters)) return;
	ch->closed = true;
	if (has_xform(ch) && !ch->putters) step_complete(ch, chv);
	drain_to_takers(ch, chv, ws);
	if (ch->count) return;
	ws->port = chv;
	for (;;) {
		bool   stale;
		qnode *t = pop_live(&ch->takers, &ch->takers_tail, &ch->ntakers, NULL, &stale);
		if (!t) break;
		add_wake(ws, t, CLJ_NIL, true);
	}
}

clj_value clj_chan_close(clj_value chv) {
	if (chan_arg(chv, "close!") == CLJ_THROWN) return CLJ_THROWN;
	clj_chan *ch = chan_of(chv);
	wakes     ws = {.n = 0};
	chan_lock(ch);
	close_locked(ch, chv, &ws);
	chan_unlock(ch);
	flush_wakes(&ws, true);
	return CLJ_NIL;
}

bool clj_chan_closed(clj_value chv) {
	clj_chan *ch = chan_of(chv);
	chan_lock(ch);
	bool closed = ch->closed;
	chan_unlock(ch);
	return closed;
}

// ---- alts!

static clj_value kw_default, kw_priority;

static void intern_keywords(void) {
	kw_default = clj_keyword_from_cstr("default");
	kw_priority = clj_keyword_from_cstr("priority");
}

static void shuffle(uint32_t *order, uint32_t n) {
	static _Thread_local uint64_t state;
	if (!state) state = (uint64_t)(uintptr_t)&state * 0x9E3779B97F4A7C15ull | 1;
	for (uint32_t i = n; i > 1; i--) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		uint32_t j = (uint32_t)(state % i), t = order[i - 1];
		order[i - 1] = order[j];
		order[j] = t;
	}
}

static clj_value pair(clj_value a, clj_value b) {
	clj_value items[2] = {a, b};
	return clj_vector_from_array(items, 2);
}

static clj_value check_ports(clj_value ports, uint32_t n) {
	for (uint32_t i = 0; i < n; i++) {
		clj_value port = clj_vector_nth(ports, i);
		if (clj_is_vector(port)) {
			if (clj_vector_count(port) != 2 || !clj_is_chan(clj_vector_nth(port, 0))) return clj_throw_msg("alts! put operation must be [channel value]");
			if (clj_is_nil(clj_vector_nth(port, 1))) return clj_throw_msg("Can't put nil on channel");
			if (reentry(chan_of(clj_vector_nth(port, 0)), "alts!")) return CLJ_THROWN;
		} else if (!clj_is_chan(port)) {
			return clj_throw_msg("alts! expects channels or [channel value] pairs, got: %s", clj_type_name(port));
		} else if (reentry(chan_of(port), "alts!")) {
			return CLJ_THROWN;
		}
	}
	return CLJ_NIL;
}

// One waiter in every port's queue; the paired claim decides the winner, the continuation moves to whoever won.
clj_value clj_chan_alts(clj_value ports, clj_value opts) {
	if (!clj_is_vector(ports)) return clj_throw_msg("alts! expects a vector of ports, got: %s", clj_type_name(ports));
	uint32_t n = clj_vector_count(ports);
	if (n == 0) return clj_throw_msg("alts! must have at least one channel operation");
	if (check_ports(ports, n) == CLJ_THROWN) return CLJ_THROWN;
	bool      has_default = false, priority = false;
	clj_value dflt = CLJ_NIL;
	if (clj_is_map(opts)) {
		dflt = clj_map_get(opts, kw_default, CLJ_UNBOUND);
		has_default = dflt != CLJ_UNBOUND;
		if (!has_default) dflt = CLJ_NIL;
		priority = clj_truthy(clj_map_get(opts, kw_priority, CLJ_NIL));
	}
	if (!has_default && !clj_park_allowed()) return CLJ_THROWN;
	uint32_t small[8], *order = n <= 8 ? small : malloc(n * sizeof *order);
	if (!order) clj_fatal("out of memory");
	for (uint32_t i = 0; i < n; i++) order[i] = i;
	if (!priority) shuffle(order, n);
	clj_waiter *w = clj_waiter_new(clj_coro_current(), CLJ_NIL);
	clj_value   result = CLJ_UNBOUND;
	bool        stale = false;
	for (uint32_t k = 0; k < n && result == CLJ_UNBOUND && !stale; k++) {
		uint32_t  i = order[k];
		clj_value port = clj_vector_nth(ports, i);
		bool      is_put = clj_is_vector(port);
		clj_value chv = is_put ? clj_vector_nth(port, 0) : port;
		clj_value v = is_put ? clj_vector_nth(port, 1) : CLJ_NIL;
		clj_chan *ch = chan_of(chv);
		if (is_put) clj_share(v);
		wakes     ws = {.n = 0};
		bool      ok = false;
		clj_value out = CLJ_NIL;
		chan_lock(ch);
		int r = is_put ? put_locked(ch, chv, v, w, &ok, &ws) : take_locked(ch, chv, w, &out, &ws);
		if (r == OP_DONE) {
			result = pair(is_put ? clj_bool(ok) : out, chv);
			clj_release(out);
		} else if (r == OP_STALE) {
			stale = true;
		} else if (!has_default) {
			if (pending_full(is_put ? &ch->putters : &ch->takers, is_put ? &ch->putters_tail : &ch->takers_tail, is_put ? &ch->nputters : &ch->ntakers)) {
				chan_unlock(ch);
				if (order != small) free(order);
				if (clj_waiter_claim(w)) {
					clj_waiter_release(w);
					return pending_error(is_put);
				}
				clj_park(w);
				result = pair(w->value, w->port);
				clj_waiter_release(w);
				return result;
			}
			if (is_put) {
				enqueue(&ch->putters, &ch->putters_tail, node_new(w, clj_retain(v), i));
				ch->nputters++;
			} else {
				enqueue(&ch->takers, &ch->takers_tail, node_new(w, CLJ_NIL, i));
				ch->ntakers++;
			}
		}
		chan_unlock(ch);
		flush_wakes(&ws, true);
	}
	if (order != small) free(order);
	if (result != CLJ_UNBOUND) {
		clj_waiter_release(w);
		return result;
	}
	if (has_default && !stale) {
		if (clj_waiter_claim(w)) {
			clj_waiter_release(w);
			return pair(dflt, kw_default);
		}
	}
	clj_park(w);
	if (cancelled_here()) {
		clj_waiter_release(w);
		return cancelled_throw();
	}
	result = pair(w->value, w->port);
	clj_waiter_release(w);
	return result;
}

// ---- timeout

static void close_timer(void *ctx) {
	clj_value chv = clj_from_ptr(ctx);
	clj_release(clj_chan_close(chv));
	clj_release(chv);
}

clj_value clj_chan_timeout(int64_t ms) {
	clj_value chv = clj_chan_new(CLJ_NIL);
	clj_share(chv);
	clj_retain(chv);
	clj_sched_timer(ms < 0 ? 0 : (uint64_t)ms * 1000000u, close_timer, clj_to_ptr(chv));
	return chv;
}

// ---- promise, deref and deliver: a promise buffer serves its one value to every taker for ever

clj_value clj_chan_promise(void) { return chan_alloc(CLJ_BUF_PROMISE, 1, CLJ_CHAN_PROMISE); }

static bool realized_locked(const clj_chan *ch) { return ch->count > 0 || ch->closed; }

// A cancelled future is done at once (the JVM's isDone): its deref throws the cancellation as soon as the body lands.
bool clj_chan_realized(clj_value chv) {
	clj_chan *ch = chan_of(chv);
	chan_lock(ch);
	bool r = realized_locked(ch) || (ch->role == CLJ_CHAN_FUTURE && !clj_is_nil(ch->coro) && clj_coro_cancelled(ch->coro));
	chan_unlock(ch);
	return r;
}

// deliver of nil closes the channel: takes then answer nil for ever, and realized? is true.
clj_value clj_chan_deliver(clj_value chv, clj_value v) {
	if (chan_arg(chv, "deliver") == CLJ_THROWN) return CLJ_THROWN;
	clj_chan *ch = chan_of(chv);
	if (ch->kind != CLJ_BUF_PROMISE) return clj_throw_msg("deliver expects a promise, got a channel");
	clj_share(v);
	wakes ws = {.n = 0};
	chan_lock(ch);
	bool first = !realized_locked(ch);
	if (first) {
		if (clj_is_nil(v)) {
			close_locked(ch, chv, &ws);
		} else {
			bool ok;
			put_locked(ch, chv, v, NULL, &ok, &ws);
		}
	}
	chan_unlock(ch);
	flush_wakes(&ws, true);
	return first ? clj_retain(chv) : CLJ_NIL;
}

// A future's cached exception is thrown again by every deref (the JVM wraps it in an ExecutionException).
static clj_value rethrow_error(clj_chan *ch, clj_value v) {
	chan_lock(ch);
	clj_value error = clj_retain(ch->error);
	chan_unlock(ch);
	if (clj_is_nil(error)) return v;
	clj_release(v);
	return clj_throw(error);
}

clj_value clj_chan_deref(clj_value chv) {
	clj_value v = clj_chan_take(chv);
	if (v == CLJ_THROWN) return CLJ_THROWN;
	return rethrow_error(chan_of(chv), v);
}

clj_value clj_chan_deref_timeout(clj_value chv, int64_t ms, clj_value timeout_val) {
	if (chan_arg(chv, "deref") == CLJ_THROWN) return CLJ_THROWN;
	if (clj_chan_realized(chv)) return clj_chan_deref(chv);
	clj_value t = clj_chan_timeout(ms);
	clj_value items[2] = {chv, t};
	clj_value ports = clj_vector_from_array(items, 2);
	clj_value opts = clj_map_assoc(clj_map_empty(), kw_priority, CLJ_TRUE);
	clj_value r = clj_chan_alts(ports, opts);
	clj_release(opts);
	clj_release(ports);
	clj_release(t);
	if (r == CLJ_THROWN) return CLJ_THROWN;
	clj_value port = clj_vector_nth(r, 1), v = clj_retain(clj_vector_nth(r, 0));
	bool      won = port == chv;
	clj_release(r);
	if (!won) {
		clj_release(v);
		return clj_retain(timeout_val);
	}
	return rethrow_error(chan_of(chv), v);
}

// ---- go, future and thread

// The body's value goes on as a fire-and-forget put, then the channel closes (core.async's go).
static void deliver_result(clj_value chv, clj_value v) {
	clj_chan *ch = chan_of(chv);
	wakes     ws = {.n = 0};
	chan_lock(ch);
	if (!clj_is_nil(v)) {
		clj_share(v);
		bool ok;
		if (put_locked(ch, chv, v, NULL, &ok, &ws) == OP_NOT_READY) {
			enqueue(&ch->putters, &ch->putters_tail, node_new(NULL, clj_retain(v), 0));
			ch->nputters++;
		}
	}
	close_locked(ch, chv, &ws);
	chan_unlock(ch);
	flush_wakes(&ws, true);
}

static void go_done(clj_coro *c, void *ctx) {
	clj_value chv = clj_from_ptr(ctx);
	if (c->threw) clj_coro_report_uncaught(c);
	deliver_result(chv, c->threw ? CLJ_NIL : c->result);
	clj_release(chv);
}

// A future keeps the thrown value for its derefs instead of reporting it.
static void future_done(clj_coro *c, void *ctx) {
	clj_value chv = clj_from_ptr(ctx);
	clj_chan *ch = chan_of(chv);
	if (c->threw) {
		chan_lock(ch);
		ch->error = clj_retain(c->result);
		chan_unlock(ch);
	}
	deliver_result(chv, c->threw ? CLJ_NIL : c->result);
	clj_release(chv);
}

static clj_value spawn_into(clj_value f, clj_value chv, int affinity, void (*done)(clj_coro *c, void *ctx)) {
	clj_share(chv);
	clj_retain(chv);
	clj_value coro = clj_coro_spawn(f, NULL, 0, affinity, done, clj_to_ptr(chv));
	if (coro == CLJ_THROWN) {
		clj_release(chv);
		clj_release(chv);
		return CLJ_THROWN;
	}
	clj_chan *ch = chan_of(chv);
	chan_lock(ch);
	ch->coro = coro;
	chan_unlock(ch);
	return chv;
}

clj_value clj_chan_go(clj_value f, int affinity) {
	if (!clj_has_core(f, CLJ_CORE_FN)) return clj_throw_msg("go expects a fn, got: %s", clj_type_name(f));
	return spawn_into(f, clj_chan_new(CLJ_NIL), affinity, go_done);
}

clj_value clj_chan_future(clj_value f) {
	if (!clj_has_core(f, CLJ_CORE_FN)) return clj_throw_msg("future-call expects a fn, got: %s", clj_type_name(f));
	return spawn_into(f, chan_alloc(CLJ_BUF_PROMISE, 1, CLJ_CHAN_FUTURE), CLJ_AFFINITY_POOL, future_done);
}

struct thread_job {
	clj_value    f, chv;
	void        *bindings;
	void        *captures;
	_Atomic bool cancel_early; // a cancel! before the job's thread attached its coroutine
};

// Runs on a blocking-pool thread as its implicit coroutine, with the spawner's bindings conveyed. The channel
// holds the implicit coroutine while the body runs, so cancel! reaches it; the flag is reset for the thread's
// next job.
static void thread_run(void *ctx) {
	thread_job *j = ctx;
	clj_coro   *c = clj_coro_current();
	clj_chan   *ch = chan_of(j->chv);
	chan_lock(ch);
	ch->coro = clj_retain(clj_from_ptr(c));
	ch->job = NULL;
	bool early = atomic_load_explicit(&j->cancel_early, memory_order_relaxed);
	chan_unlock(ch);
	if (early) clj_coro_cancel_kind(c, CLJ_CANCEL_REQUESTED);
	void *saved = c->bindings, *saved_captures = c->captures;
	c->bindings = j->bindings;
	c->captures = j->captures;
	clj_eval_top_enter();
	clj_value r = clj_invoke(j->f, NULL, 0);
	clj_eval_top_leave();
	if (r == CLJ_THROWN) {
		clj_coro_report_uncaught(c);
		clj_coro_drop_pending(c);
		r = CLJ_NIL;
	}
	c->bindings = saved;
	c->captures = saved_captures;
	clj_var_bindings_release(j->bindings);
	clj_output_captures_release(j->captures);
	chan_lock(ch);
	clj_value coro = ch->coro;
	ch->coro = CLJ_NIL;
	chan_unlock(ch);
	clj_release(coro);
	clj_coro_cancel_reset(c);
	deliver_result(j->chv, r);
	clj_release(r);
	clj_release(j->f);
	clj_release(j->chv);
	free(j);
}

clj_value clj_chan_thread(clj_value f) {
	if (!clj_has_core(f, CLJ_CORE_FN)) return clj_throw_msg("thread* expects a fn, got: %s", clj_type_name(f));
	clj_value   chv = chan_alloc(CLJ_BUF_NONE, 0, CLJ_CHAN_THREAD);
	thread_job *j = malloc(sizeof *j);
	if (!j) clj_fatal("out of memory");
	clj_share(f);
	clj_share(chv);
	j->f = clj_retain(f);
	j->chv = clj_retain(chv);
	j->bindings = clj_var_bindings_share();
	j->captures = clj_output_captures_share();
	atomic_init(&j->cancel_early, false);
	chan_of(chv)->job = j;
	clj_blocking_detach(thread_run, j);
	return chv;
}

// True when a body still running (or not yet started) was told to stop; false once it finished.
static clj_value cancel_chan(clj_value chv, int kind, clj_value cause) {
	if (chan_arg(chv, "cancel!") == CLJ_THROWN) return CLJ_THROWN;
	clj_chan *ch = chan_of(chv);
	bool      cancelled = false;
	clj_value coro = CLJ_NIL;
	chan_lock(ch);
	if (ch->job) {
		atomic_store_explicit(&ch->job->cancel_early, true, memory_order_relaxed);
		cancelled = true;
	} else if (!clj_is_nil(ch->coro)) {
		coro = clj_retain(ch->coro);
	}
	chan_unlock(ch);
	if (clj_is_nil(coro)) return clj_bool(cancelled);
	// The wake is outside the section: a thread's implicit coroutine is cancellable only through its channel.
	if (ch->role == CLJ_CHAN_THREAD) {
		clj_coro_cancel_kind_cause(clj_coro_of(coro), kind, cause);
		cancelled = true;
	} else if (!clj_coro_done(coro)) {
		if (!clj_coro_of(coro)->implicit) clj_coro_cancel_kind_cause(clj_coro_of(coro), kind, cause);
		cancelled = true;
	}
	clj_release(coro);
	return clj_bool(cancelled);
}

clj_value clj_chan_cancel(clj_value chv) { return cancel_chan(chv, CLJ_CANCEL_REQUESTED, CLJ_NIL); }

// Not CLJ_CANCEL_SCOPE: that kind is cleared by a scope's exit, and a sibling's own nested scope would clear it.
clj_value clj_chan_cancel_cause(clj_value chv, clj_value cause) { return cancel_chan(chv, CLJ_CANCEL_REQUESTED, cause); }

bool clj_chan_cancelled(clj_value chv) {
	clj_chan *ch = chan_of(chv);
	chan_lock(ch);
	bool r = !clj_is_nil(ch->coro) && clj_coro_cancelled(ch->coro);
	chan_unlock(ch);
	return r;
}

int clj_chan_role(clj_value chv) { return chan_of(chv)->role; }

clj_value clj_debug_chan_coro(clj_value chv) {
	clj_chan *ch = chan_of(chv);
	chan_lock(ch);
	clj_value coro = clj_retain(ch->coro);
	chan_unlock(ch);
	return coro;
}

uint32_t clj_debug_chan_pending(clj_value chv, bool puts) {
	clj_chan *ch = chan_of(chv);
	chan_lock(ch);
	uint32_t n = puts ? ch->nputters : ch->ntakers;
	chan_unlock(ch);
	return n;
}

// ---- builtins

static clj_value b_chan(const clj_value *args, size_t n) {
	if (n < 2) return clj_chan_new(n ? args[0] : CLJ_NIL);
	return clj_chan_new_xform(args[0], args[1], n > 2 ? args[2] : CLJ_NIL);
}

static clj_value buffer_of_kind(const clj_value *args, int kind, const char *name) {
	if (!clj_is_fixnum(args[0]) || clj_fixnum_val(args[0]) <= 0) return clj_throw_msg("%s expects a positive size", name);
	return clj_buffer_new(kind, (uint32_t)clj_fixnum_val(args[0]));
}

static clj_value b_buffer(const clj_value *args, size_t n) {
	(void)n;
	return buffer_of_kind(args, CLJ_BUF_FIXED, "buffer");
}

static clj_value b_dropping_buffer(const clj_value *args, size_t n) {
	(void)n;
	return buffer_of_kind(args, CLJ_BUF_DROPPING, "dropping-buffer");
}

static clj_value b_sliding_buffer(const clj_value *args, size_t n) {
	(void)n;
	return buffer_of_kind(args, CLJ_BUF_SLIDING, "sliding-buffer");
}

static clj_value b_promise_buffer(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_buffer_new(CLJ_BUF_PROMISE, 1);
}

static clj_value b_unblocking_buffer_p(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_buffer(args[0])) return CLJ_FALSE;
	int kind = ((clj_buffer *)clj_to_ptr(args[0]))->kind;
	return clj_bool(kind == CLJ_BUF_DROPPING || kind == CLJ_BUF_SLIDING || kind == CLJ_BUF_PROMISE);
}

static clj_value b_take(const clj_value *args, size_t n) { return chan_take(args[0], n > 1 && clj_truthy(args[1])); }

static clj_value b_put(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_put(args[0], args[1]);
}

static clj_value b_poll(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_poll(args[0]);
}

static clj_value b_offer(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_offer(args[0], args[1]);
}

static clj_value b_put_cb(const clj_value *args, size_t n) { return clj_chan_put_cb(args[0], args[1], n > 2 ? args[2] : CLJ_NIL, n > 3 ? clj_truthy(args[3]) : true); }

static clj_value b_take_cb(const clj_value *args, size_t n) { return clj_chan_take_cb(args[0], args[1], n > 2 ? clj_truthy(args[2]) : true); }

static clj_value b_close(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_close(args[0]);
}

static clj_value b_alts(const clj_value *args, size_t n) { return clj_chan_alts(args[0], n > 1 ? args[1] : CLJ_NIL); }

static clj_value b_timeout(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_fixnum(args[0])) return clj_throw_msg("timeout expects milliseconds, got: %s", clj_type_name(args[0]));
	return clj_chan_timeout(clj_fixnum_val(args[0]));
}

static clj_value b_go(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_go(args[0], CLJ_AFFINITY_POOL);
}

static clj_value b_go_main(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_go(args[0], CLJ_AFFINITY_MAIN);
}

static clj_value b_thread(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_thread(args[0]);
}

static clj_value b_future(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_future(args[0]);
}

static clj_value b_promise(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_chan_promise();
}

static clj_value b_deliver(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_deliver(args[0], args[1]);
}

static clj_value b_deref(const clj_value *args, size_t n) {
	if (chan_arg(args[0], "deref") == CLJ_THROWN) return CLJ_THROWN;
	if (n == 1) return clj_chan_deref(args[0]);
	if (!clj_is_fixnum(args[1])) return clj_throw_msg("deref expects a timeout in milliseconds, got: %s", clj_type_name(args[1]));
	return clj_chan_deref_timeout(args[0], clj_fixnum_val(args[1]), args[2]);
}

static clj_value b_realized_p(const clj_value *args, size_t n) {
	(void)n;
	if (chan_arg(args[0], "realized?") == CLJ_THROWN) return CLJ_THROWN;
	return clj_bool(clj_chan_realized(args[0]));
}

static clj_value b_cancel(const clj_value *args, size_t n) {
	(void)n;
	return clj_chan_cancel(args[0]);
}

static clj_value b_cancelled_p(const clj_value *args, size_t n) {
	(void)n;
	if (chan_arg(args[0], "future-cancelled?") == CLJ_THROWN) return CLJ_THROWN;
	return clj_bool(clj_chan_cancelled(args[0]));
}

static clj_value b_chan_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_chan(args[0]));
}

static clj_value b_future_p(const clj_value *args, size_t n) {
	(void)n;
	return clj_bool(clj_is_chan(args[0]) && clj_chan_role(args[0]) == CLJ_CHAN_FUTURE);
}

static clj_value b_closed_p(const clj_value *args, size_t n) {
	(void)n;
	if (chan_arg(args[0], "closed?") == CLJ_THROWN) return CLJ_THROWN;
	return clj_bool(clj_chan_closed(args[0]));
}

static clj_value b_sleep(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_fixnum(args[0])) return clj_throw_msg("sleep expects milliseconds, got: %s", clj_type_name(args[0]));
	return clj_sched_sleep_ms(clj_fixnum_val(args[0]));
}

static clj_value b_available_processors(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_fixnum((int64_t)clj_sched_carrier_count());
}

// The uncaught-exception report for a library's default handler (pipeline's ex-handler on the JVM).
static clj_value b_uncaught_report(const clj_value *args, size_t n) {
	(void)n;
	clj_coro *c = clj_coro_current();
	clj_throw(clj_retain(args[0]));
	clj_coro_report_uncaught(c);
	clj_coro_drop_pending(c);
	return CLJ_NIL;
}

// The running execution as a handle for a scope (async.clj go-scoped): its children cancel it on their failure.
static clj_value b_coro_current(const clj_value *args, size_t n) {
	(void)args;
	(void)n;
	return clj_retain(clj_from_ptr(clj_coro_current()));
}

static clj_value b_coro_cancel_scope(const clj_value *args, size_t n) {
	if (!clj_is_coro(args[0])) return clj_throw_msg("coro-cancel-scope* expects a coroutine, got: %s", clj_type_name(args[0]));
	clj_coro_cancel_kind_cause(clj_coro_of(args[0]), CLJ_CANCEL_SCOPE, n > 1 ? args[1] : CLJ_NIL);
	return CLJ_NIL;
}

static clj_value b_chan_cancel_cause(const clj_value *args, size_t n) {
	return clj_chan_cancel_cause(args[0], n > 1 ? args[1] : CLJ_NIL);
}

static clj_value b_coro_uncancel_scope(const clj_value *args, size_t n) {
	(void)n;
	if (!clj_is_coro(args[0])) return clj_throw_msg("coro-uncancel-scope* expects a coroutine, got: %s", clj_type_name(args[0]));
	clj_coro_uncancel_scope(clj_coro_of(args[0]));
	return CLJ_NIL;
}

// Thread/sleep for library code: a namespace Thread with sleep, so the JVM's static call resolves (NOTES.md).
static void install_thread_ns(void) {
	clj_value ns_name = clj_symbol_from_cstr("Thread");
	clj_value ns = clj_ns_find_or_create(ns_name);
	clj_value name = clj_symbol_from_cstr("sleep");
	clj_value qualified = clj_symbol_new(clj_symbol_name(ns_name), clj_symbol_name(name));
	clj_value f = clj_fn_native(qualified, b_sleep, 1, 1);
	clj_var_bind_root(clj_ns_intern(ns, name), f);
	clj_release(f);
	clj_release(qualified);
	clj_release(name);
	clj_release(ns_name);
}

void clj_chan_install(void) {
	intern_keywords();
	clj_value rf_name = clj_symbol_from_cstr("clojure.core/chan-rf*");
	chan_rf = clj_fn_native(rf_name, b_chan_rf, 1, 2);
	clj_release(rf_name);
	clj_header_of(chan_rf)->flags |= CLJ_FLAG_IMMORTAL;
	static const struct {
		const char   *name;
		clj_native_fn fn;
		uint32_t      min, max;
	} entries[] = {
		{"chan*", b_chan, 0, 3},            {"buffer*", b_buffer, 1, 1},          {"dropping-buffer*", b_dropping_buffer, 1, 1},
		{"sliding-buffer*", b_sliding_buffer, 1, 1}, {"promise-buffer*", b_promise_buffer, 0, 0}, {"unblocking-buffer?*", b_unblocking_buffer_p, 1, 1},
		{"chan-take*", b_take, 1, 2},       {"chan-put*", b_put, 2, 2},
		{"chan-poll*", b_poll, 1, 1},       {"chan-offer*", b_offer, 2, 2},       {"chan-put-cb*", b_put_cb, 2, 4},
		{"chan-take-cb*", b_take_cb, 2, 3}, {"chan-close*", b_close, 1, 1},       {"chan-alts*", b_alts, 1, 2},
		{"chan-timeout*", b_timeout, 1, 1}, {"coro-go*", b_go, 1, 1},             {"coro-go-main*", b_go_main, 1, 1},
		{"thread*", b_thread, 1, 1},        {"chan-cancel*", b_cancel, 1, 1},     {"chan?*", b_chan_p, 1, 1},
		{"chan-closed?*", b_closed_p, 1, 1}, {"future*", b_future, 1, 1},         {"promise*", b_promise, 0, 0},
		{"chan-deliver*", b_deliver, 2, 2}, {"chan-deref*", b_deref, 1, 3},       {"chan-realized?*", b_realized_p, 1, 1},
		{"chan-cancelled?*", b_cancelled_p, 1, 1}, {"future?*", b_future_p, 1, 1}, {"sleep*", b_sleep, 1, 1},
		{"available-processors*", b_available_processors, 0, 0}, {"coro-current*", b_coro_current, 0, 0}, {"uncaught-report*", b_uncaught_report, 1, 1},
		{"coro-cancel-scope*", b_coro_cancel_scope, 1, 2}, {"coro-uncancel-scope*", b_coro_uncancel_scope, 1, 1},
		{"chan-cancel-cause*", b_chan_cancel_cause, 1, 2},
	};
	for (size_t i = 0; i < sizeof entries / sizeof *entries; i++) clj_builtin_bind(entries[i].name, entries[i].fn, entries[i].min, entries[i].max);
	install_thread_ns();
}
