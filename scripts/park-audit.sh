#!/bin/sh
# @ai-generated(solo)
# The collector judges a parked coroutine by its wake source (design §7, «Фаза 3»): no switch out may skip naming it.
# Register: switch in/out, the finish, the bench's bounce and clj_park, plus the two declarations.
set -e
cd "$(dirname "$0")/.."

register='Sources/CljCore/coro.c 5
Sources/CljCore/coro_internal.h 2
Sources/CljCore/sched.c 1'

found=$(grep -rcE 'clj_coro_switch_out\(|clj_ctx_switch\(' Sources 2>/dev/null |
	grep -v ':0$' | grep -v '^Sources/CljCore/boot/' | tr ':' ' ' | sort)

fail=0
if [ "$found" != "$(echo "$register" | sort)" ]; then
	echo "park-audit: the switch sites changed; a new park goes through clj_park (see $0 and NOTES.md, Coroutines)"
	echo "--- registered ---"
	echo "$register" | sort
	echo "--- found ---"
	echo "$found"
	fail=1
fi

# A call names its wake source by a constructor; the declarations and clj_lot_park's forwarding of its own are the
# only calls without one.
unnamed=$(grep -rnE 'clj_(lot_)?park\(' Sources --include='*.c' --include='*.h' 2>/dev/null |
	grep -v '^Sources/CljCore/boot/' | grep -v 'clj_wake_[a-z_]*()' |
	grep -vE 'clj_wake wake\)( \{|;)$' | grep -v 'Sources/CljCore/cmutex.c:.*clj_park(w, wake);' || true)
if [ -n "$unnamed" ]; then
	echo "park-audit: a park without a clj_wake_* constructor:"
	echo "$unnamed"
	fail=1
fi

literal=$(grep -rnE '\(clj_wake\) *\{' Sources --include='*.c' --include='*.h' 2>/dev/null | grep -v '^Sources/CljCore/coro_internal.h:' || true)
if [ -n "$literal" ]; then
	echo "park-audit: a clj_wake literal outside its constructors:"
	echo "$literal"
	fail=1
fi

[ "$fail" = 0 ] || exit 1
echo "park-audit: every switch out of a coroutine is a park that names its wake source"
