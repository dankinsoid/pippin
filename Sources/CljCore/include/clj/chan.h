// @ai-generated(solo)
#ifndef CLJ_CHAN_H
#define CLJ_CHAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "object.h"

// core.async channels (design §4): a buffer and two queues of parked waiters under a clj_lock, direct hand-off.
extern const clj_type clj_chan_type;
extern const clj_type clj_buffer_type;

enum { CLJ_BUF_NONE = 0, CLJ_BUF_FIXED = 1, CLJ_BUF_DROPPING = 2, CLJ_BUF_SLIDING = 3, CLJ_BUF_PROMISE = 4 };
// What made the channel: a promise and a future are promise-buffered channels, so deref, alts! and <! all wait on them.
enum { CLJ_CHAN_PLAIN = 0, CLJ_CHAN_PROMISE = 1, CLJ_CHAN_FUTURE = 2, CLJ_CHAN_THREAD = 3 };

// The JVM's limit on pending puts and takes per channel.
#define CLJ_CHAN_MAX_PENDING 1024

// (buffer n), (dropping-buffer n), (sliding-buffer n): a spec object, owned.
clj_value clj_buffer_new(int kind, uint32_t cap);
// (chan), (chan n), (chan buf); a nil or 0 argument is unbuffered. Owned.
clj_value clj_chan_new(clj_value buf_or_n);
// (chan buf xform ex-handler): the transducer's step runs under the channel's coroutine mutex (NOTES.md, "Channels").
clj_value clj_chan_new_xform(clj_value buf_or_n, clj_value xform, clj_value ex_handler);
// (promise): a promise-buffered channel; deliver puts once (nil closes), deref takes without consuming.
clj_value clj_chan_promise(void);
clj_value clj_chan_deliver(clj_value ch, clj_value v);
// @ch: a take that rethrows a future's cached exception; the timed form answers timeout_val past ms.
clj_value clj_chan_deref(clj_value ch);
clj_value clj_chan_deref_timeout(clj_value ch, int64_t ms, clj_value timeout_val);
bool      clj_chan_realized(clj_value ch);
// (future-call f): a pool coroutine whose value or exception the promise-buffered channel keeps.
clj_value clj_chan_future(clj_value f);
bool      clj_chan_cancelled(clj_value ch);
int       clj_chan_role(clj_value ch);

static inline bool clj_is_chan(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_chan_type; }
static inline bool clj_is_buffer(clj_value v) { return clj_is_ptr(v) && clj_header_of(v)->type == &clj_buffer_type; }

// <! and >! park until done (a bare thread blocks); CLJ_THROWN on a cancellation, an illegal park or a nil put.
clj_value clj_chan_take(clj_value ch);
clj_value clj_chan_put(clj_value ch, clj_value v);
// poll! and offer!: never park; nil when not ready (offer!: false on a closed channel).
clj_value clj_chan_poll(clj_value ch);
clj_value clj_chan_offer(clj_value ch, clj_value v);
// put!/take! with a callback fn (or nil): the callback runs on whoever completes the operation.
clj_value clj_chan_put_cb(clj_value ch, clj_value v, clj_value fn, bool on_caller);
clj_value clj_chan_take_cb(clj_value ch, clj_value fn, bool on_caller);
clj_value clj_chan_close(clj_value ch);
bool      clj_chan_closed(clj_value ch);
// (alts! ports opts): ports a vector of channels or [ch v] pairs; opts a map with :default and :priority.
clj_value clj_chan_alts(clj_value ports, clj_value opts);
// A channel closed after ms milliseconds by the timer thread.
clj_value clj_chan_timeout(int64_t ms);
// (go* f) and (go-main* f): the body's value is put on the returned channel, which is then closed.
clj_value clj_chan_go(clj_value f, int affinity);
// (thread* f): the body runs on a blocking-pool thread; the channel gets its value.
clj_value clj_chan_thread(clj_value f);
// cancel! of the go, future or thread behind a channel: true while its body had not finished.
clj_value clj_chan_cancel(clj_value ch);
// Debug: pending puts and takes queued on the channel.
uint32_t clj_debug_chan_pending(clj_value ch, bool puts);

void clj_chan_install(void);

#endif
