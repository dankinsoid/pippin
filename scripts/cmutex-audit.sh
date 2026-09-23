#!/bin/sh
# @ai-generated(solo)
# Fails when the set of clj_cmutex acquisition sites differs from the register below.
# cmutex_held is bumped by hand (NOTES.md, suspension), so an unregistered site is a suspend parked holding a mutex.
# Adding one: bump cmutex_held if user code can run while it is held, and update the count here either way.
set -e
cd "$(dirname "$0")/.."

register='Sources/CljCore/atom.c 1
Sources/CljCore/chan.c 1
Sources/CljCore/cmutex.c 1'

found=$(grep -rcE 'clj_cmutex_lock\(|clj_cmutex_trylock\(' Sources 2>/dev/null |
	grep -v ':0$' | grep -v '/include/clj/cmutex.h:' | grep -v '^Sources/CljCore/boot/' |
	tr ':' ' ' | sort)

if [ "$found" != "$(echo "$register" | sort)" ]; then
	echo "cmutex-audit: the cmutex sites changed; see the register in $0 and NOTES.md"
	echo "--- registered ---"
	echo "$register" | sort
	echo "--- found ---"
	echo "$found"
	exit 1
fi
echo "cmutex-audit: $(echo "$found" | wc -l | tr -d ' ') files hold a cmutex, each accounted for"
