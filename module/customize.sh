# android-bridge installer, sourced by the Magisk, KernelSU or APatch installer.
# Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# Nothing is extracted for us, so only this device's binary lands in $MODPATH
SKIPUNZIP=1

unzip -o "$ZIPFILE" module.prop post-fs-data.sh sepolicy.rule -d "$MODPATH" >&2
unzip -oj "$ZIPFILE" "bin/$ARCH/android-bridge" -d "$MODPATH/bin" >&2
[ -f "$MODPATH/bin/android-bridge" ] || abort "! No android-bridge binary for $ARCH"

set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/bin/android-bridge" 0 0 0755

ui_print "- Installed android-bridge for $ARCH"
ui_print "- The socket appears at /dev/android-bridge/bridge.sock after a reboot"
