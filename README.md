# android-bridge

A shell on Android that root inside a Droidspaces container can reach. On a homelab
phone nobody touches, it is how a container reboots the phone, runs `pm`, `cmd` or
`dumpsys`, or manages Droidspaces itself. `reboot` inside a container only reboots
the container.

It is a separate project from Droidspaces and ships as its own root module, for
KernelSU, Magisk or APatch. A daemon started at boot listens on
`/dev/android-bridge/bridge.sock`, and every connection gets `/system/bin/sh` as the
shell user, uid 2000, in `u:r:shell:s0`, with adbd's groups and capability bounding
set. That is what `adb shell` gives you, and it is never root. Run `su` in it for
root, which works only if the root manager grants su to Shell.

## Who can connect

Only real root. The directory is 0700 and the socket 0600, both owned by root, so any
other uid gets `EACCES` from the kernel, inside a container too. The daemon then wants
uid 0 as the host sees it, in its own user namespace, so root inside a user namespace,
such as rootless podman or `unshare -r`, is refused. Each check denies when it cannot
tell.

Droidspaces containers do not use user namespaces, so container root is host uid 0
and passes. Every container that gets the socket can open a shell on the phone. Do
not give it to a container that runs untrusted code as root.

## Install

1. Build the zip with `make module`, see Build below, or take it from a release.
2. Flash `android-bridge-module-v*.zip` in the root manager and reboot.
3. Check that the daemon started: its log, see Logs below, has a
   `listening on /dev/android-bridge/bridge.sock` line.
4. In Droidspaces, give each container you want to reach the phone a read-only bind
   mount from `/dev/android-bridge` to `/run/android-bridge`, then restart it. On the
   command line that is `--bind=/dev/android-bridge:/run/android-bridge:ro`. The
   directory is mounted rather than the socket, so a restarted daemon is still
   reachable.
5. Copy the client into the container, for example to
   `/usr/local/bin/android-bridge`, and make it executable. It is the same static
   binary as the daemon: `/data/adb/modules/android-bridge/bin/android-bridge` on the
   phone, or `bin/arm64/android-bridge` inside the zip for an aarch64 phone. The zip
   uses the root manager's arch names: `arm64`, `arm`, `x64`, `x86` and `riscv64`.

Tested on Linux on 2026-10-31: the smoke test, a simulated container with its own
mount and PID namespaces, AddressSanitizer runs with malformed and random frames, and
the installer against a mock of the manager's install functions. Tested on a Galaxy
A16 (SM-A166P) with Android 14 and KernelSU (ksud 3.3.0) on 2026-10-31, with the
daemon started by hand from `/data/local/tmp`: the shell was uid 2000 in
`u:r:shell:s0` with no capabilities, and exit codes, stdin, stderr, terminals, 8 MB
of data and a client hangup passed through. On the same phone, with that daemon
started by hand on `/dev/android-bridge` and the bind from step 4, root in a
Droidspaces 7.1.0 Ubuntu 24.04 container running systemd ran `dumpsys`, `getprop`
and `pm` through it, a non-root user was refused, and root in a user namespace was
refused by the daemon. The module install and the start at boot are not yet tested
on a phone. If the shell refuses to start, the client prints why.

## Use

Inside the container, as root:

```
android-bridge                         # interactive shell, like adb shell
android-bridge reboot                  # reboot the phone
android-bridge dumpsys battery
echo hello | android-bridge cat        # pipes and exit codes pass through
android-bridge -t top                  # force a terminal for a command
android-bridge su -c /data/local/Droidspaces/bin/droidspaces show
```

Arguments are joined with spaces and run with `sh -c`, the same as `adb shell`, so
quote the same way. A terminal is allocated for an interactive shell on a terminal.
`-t` forces one, `-T` refuses one. The exit code is the remote command's, or 255 when
the bridge itself failed.

## Logs

`/data/adb/android-bridge.log`, in UTC, with the previous boot in `.old`. Each
connection logs the peer pid, host or container and mode when it starts, and the exit
code when it ends. Commands are not logged.

## Build

Static musl builds. Toolchains are found through `MUSL_CROSS` or `~/toolchains`, and
`scripts/install-musl.sh <arch>` installs one.

```
make                 # prints the help, builds nothing
make native          # this machine's arch, into output/android-bridge
make aarch64         # also x86_64, armhf, x86, riscv64
make all-build       # every arch
make module          # every arch, then output/android-bridge-module-v<version>.zip
make debug-hardened  # ASan, UBSan, LSan, for a glibc host
make test            # smoke test against output/android-bridge
make bump NEW=x.y.z  # set AB_VERSION in bridge.h
make format          # clang-format over src/
```

`make test` runs a real daemon on a temporary directory. It needs root and
`/system/bin/sh`, so link that to `/bin/sh` on a machine where that is fine.
`make test BIN=output/android-bridge-hardened` runs it under the sanitizers.
