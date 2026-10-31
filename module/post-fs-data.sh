#!/system/bin/sh
# Starts the android-bridge daemon before Droidspaces boots any container, so
# the socket a container's bind mount points at already exists.
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

MODDIR=${0%/*}
LOG=/data/adb/android-bridge.log

[ -f "$LOG" ] && mv -f "$LOG" "$LOG.old"

# The daemon detaches once its socket is bound, so post-fs-data is not held up,
# and its exit code already says whether the socket is there.
if "$MODDIR/bin/android-bridge" --daemon >>"$LOG" 2>&1; then
  PID=$(pgrep -f "$MODDIR/bin/android-bridge --daemon" | head -1)
  STATUS="🟢 Running (PID $PID) | Socket: /dev/android-bridge/bridge.sock"
else
  STATUS="🔴 Not running | See $LOG"
fi

# The root manager shows the description under the module, as Droidspaces does.
# It is the status at boot, not a live one.
sed -i "s#^description=.*#description=$STATUS#" "$MODDIR/module.prop"
