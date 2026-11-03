#!/bin/sh
# Smoke test for android-bridge on a Linux host, run as root.
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

BIN=$(realpath "${1:-output/android-bridge}")
[ "$(id -u)" = 0 ] || { echo "smoke: needs root"; exit 1; }
for b in sh cat stat; do
    [ -x /system/bin/$b ] || { echo "smoke: needs /system/bin/$b (ln -s /bin/$b /system/bin/$b)"; exit 1; }
done

T=$(mktemp -d)
DIR=$T/run
SOCK=$DIR/bridge.sock
fails=0
trap 'kill $DPID 2>/dev/null; rm -rf "$T"' EXIT
# Waits up to 5 s for a shell condition.
until_() { i=0; while ! eval "$1" && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done; }

"$BIN" --daemon --foreground --dir "$DIR" 2>"$T/log" &
DPID=$!
until_ '[ -S "$SOCK" ]'

# The shell gets Android's PATH, which has no /bin on a desktop.
P='export PATH=/usr/bin:/bin:$PATH;'
c() { "$BIN" -s "$SOCK" "$P" "$@"; }
check() {
    if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1: got '$2', want '$3'"; fails=$((fails + 1)); fi
}

check "socket and directory are root only" "$(stat -c %a "$DIR") $(stat -c %a "$SOCK")" "700 600"
check "shell runs as the shell user" "$(c id -u):$(c id -g)" "2000:2000"
check "no capabilities left" "$(c 'grep CapEff /proc/self/status' | tr -d '\t')" "CapEff:0000000000000000"
c 'exit 7'; check "exit code is passed through" "$?" "7"
c 'kill -9 $$'; check "death by signal is 128+sig" "$?" "137"
check "stdin reaches the shell" "$(printf hello | c cat)" "hello"
check "stderr stays stderr" "$(c 'echo out; echo err >&2' 2>/dev/null)" "out"
check "arguments join like adb" "$(c echo 'a  b' c)" "a b c"
check "a pty when asked" "$("$BIN" -s "$SOCK" -t "$P" tty </dev/null | tr -d '\r' | cut -c1-9)" "/dev/pts/"
check "no pty by default" "$(c tty </dev/null)" "not a tty"

head -c 8000000 /dev/urandom > "$T/big"
check "8 MB both ways without a deadlock" "$(c cat <"$T/big" | md5sum)" "$(md5sum <"$T/big")"

# push and pull run on the phone's own cat and stat, with no PATH of ours.
mkdir "$T/xfer"; chown 2000:2000 "$T/xfer"; chmod 711 "$T"
"$BIN" -s "$SOCK" push "$T/big" "$T/xfer/" 2>/dev/null
check "push into a directory keeps the name" "$(md5sum <"$T/xfer/big")" "$(md5sum <"$T/big")"
check "a pushed file belongs to the shell user" "$(stat -c %u:%g "$T/xfer/big")" "2000:2000"
# $T is root's, 0711: the shell user may pass through it but not create in it.
"$BIN" -s "$SOCK" push "$T/big" "$T/made" 2>/dev/null
check "push cannot write where the shell user cannot" "$? $([ -e "$T/made" ] && echo made || echo none)" "1 none"
"$BIN" -s "$SOCK" pull "$T/xfer/big" "$T/back" 2>/dev/null
check "pull to a named file" "$(md5sum <"$T/back")" "$(md5sum <"$T/big")"
"$BIN" -s "$SOCK" push "$T/big" "$T/xfer/it's a file" 2>/dev/null
check "a quote and a space in the remote name" "$(md5sum <"$T/xfer/it's a file")" "$(md5sum <"$T/big")"
echo keep >"$T/keep"
"$BIN" -s "$SOCK" pull "$T/xfer/nope" "$T/keep" 2>/dev/null
check "a pull of a missing file leaves the local file alone" "$? $(cat "$T/keep")" "1 keep"
echo secret >"$T/xfer/root-only"; chmod 600 "$T/xfer/root-only"
"$BIN" -s "$SOCK" pull "$T/xfer/root-only" "$T/keep" 2>/dev/null
check "a pull of an unreadable file leaves the local file alone" "$? $(cat "$T/keep")" "1 keep"
chmod 700 "$T"

"$BIN" -s "$SOCK" "$P" sleep 271828 & CPID=$!
until_ 'pgrep -f "^sleep 2718" >/dev/null'
kill $CPID; wait $CPID 2>/dev/null
until_ '! pgrep -f "^sleep 2718" >/dev/null'
check "client hangup ends the shell" "$(pgrep -f '^sleep 2718' | wc -l)" "0"

out=$(setpriv --reuid=1000 --regid=1000 --clear-groups "$BIN" -s "$SOCK" true 2>&1)
check "non-root stopped by file permissions ($?)" "$out" "android-bridge: permission denied, only root may use the bridge"

# Open the file permissions so the daemon's own check is the only gate left.
chmod 755 "$T" "$DIR"; chmod 666 "$SOCK"
out=$(setpriv --reuid=1000 --regid=1000 --clear-groups "$BIN" -s "$SOCK" true 2>&1)
check "non-root stopped by the daemon ($?)" "$out" "android-bridge: permission denied, only real root may connect"
if unshare -U -r true 2>/dev/null; then
    out=$(unshare -U -r "$BIN" -s "$SOCK" true 2>&1)
    check "user namespace root stopped by the daemon ($?)" "$out" "android-bridge: permission denied, only real root may connect"
else
    echo "skip user namespace root, unshare -U is not allowed here"
fi
chmod 700 "$DIR"; chmod 600 "$SOCK"

"$BIN" --daemon --foreground --dir "$DIR" 2>/dev/null
check "a second daemon leaves the first alone" "$(c echo still)" "still"

kill $DPID; wait $DPID 2>/dev/null
"$BIN" --daemon --foreground --dir "$DIR" 2>>"$T/log" & DPID=$!
until_ '[ "$(grep -c listening "$T/log")" = 2 ]'
check "a stale socket from a crash is replaced" "$(c echo back)" "back"

echo "--- daemon log"; cat "$T/log"
[ $fails = 0 ] && echo "all passed" || { echo "$fails failed"; exit 1; }
