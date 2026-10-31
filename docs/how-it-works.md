# How it works

The module starts `android-bridge --daemon` as root early in boot. It listens on
`/dev/android-bridge/bridge.sock`, a Unix socket. A container that has
`/dev/android-bridge` bind-mounted at `/run/android-bridge` can connect to it, and each
connection that passes the checks below gets its own `/system/bin/sh` on the phone. The
client in the container relays the terminal, stdin, stdout, stderr and the exit code.

## Who can connect

Only real root. The directory is 0700 and the socket 0600, both owned by root, so any
other uid gets `EACCES` from the kernel, inside a container too. Root inside a rootless
podman container is a normal uid on the host, so the kernel refuses it too. The bridge
then wants uid 0 as the host sees it, in its own user namespace, which refuses host root
that moved into a user namespace with `unshare -r`. Each check denies when it cannot
tell.

Droidspaces containers do not use user namespaces, so container root is host uid 0 and
passes. That is why every container with the bind mount can open a shell on the phone.

## The shell you get

Every connection gets what `adb shell` gives you:

- **The shell user,** uid and gid 2000, in the SELinux domain `u:r:shell:s0`.
- **adbd's groups,** so `logcat`, `getevent`, `ping` and `/sdcard` behave as they do
  over ADB.
- **No capabilities.** The bridge checks this after the switch, and refuses to run
  anything if root or a capability survived it.
- **setuid and setgid left in the bounding set,** as adbd leaves them, so `run-as` works.
- **Android's own environment,** the `ANDROID_` variables and class paths init exports.
  Nothing from the root manager's environment reaches the shell.

`su` in that shell goes through the root manager, which decides.

The module's `sepolicy.rule` lets the root manager's domain start the shell in
`u:r:shell:s0` and lets the shell use the descriptors the bridge hands it, the same
rights AOSP gives adbd.

## What was tested

On Linux on 2026-10-31: the smoke test, a simulated container with its own mount and PID
namespaces, AddressSanitizer runs with malformed and random frames, and the installer
against a mock of the manager's install functions.

On a Galaxy A16 (SM-A166P) with Android 14 and KernelSU (ksud 3.3.0) on 2026-10-31:

- **The module,** flashed from the manager, started the bridge at boot, and the module's
  SELinux rules loaded with no denials.
- **The shell** was uid 2000 in `u:r:shell:s0` with no capabilities and the same groups as
  `adb shell`. Exit codes, stdin, stderr, terminals, 8 MB of data and a client hangup
  passed through.
- **In a Droidspaces 7.1.0 Ubuntu 24.04 container** running systemd, with the bind mount,
  root ran `dumpsys`, `getprop`, `pm` and `su` through it. A non-root user was refused by
  the kernel, and root in a user namespace was refused by the bridge.
