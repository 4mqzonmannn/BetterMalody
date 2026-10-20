#!/bin/bash
# Malody Mobile の終了を待ってから apply_container.sh を実行する(最大30分)。引数はそのまま apply_container.sh に渡す。
P="$HOME/Documents/BetterMalody"
for i in $(seq 1 900); do pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null || break; sleep 2; done
if pgrep -f "me.mugzone.m.app/Malody Mobile" >/dev/null; then echo "TIMEOUT: still running"; exit 1; fi
exec "$P/tools/apply_container.sh" "$@"
