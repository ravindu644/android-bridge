# android-bridge

A shell on Android that root inside a container can reach. On a homelab phone
nobody touches, it is how a container reboots the phone, runs `pm`, `cmd` or
`dumpsys`, or manages Droidspaces itself. `reboot` inside a container only
reboots the container.

It is a separate project from Droidspaces and ships as its own root module, for
KernelSU, Magisk or APatch.
This side only creates the socket on the host. Each container gets it through a
bind mount, see Install.

## How it works

- A daemon on Android, started from the module's `post-fs-data.sh`, listens on
  `/dev/android-bridge/bridge.sock`. It starts before Droidspaces boots any
  container.
- A bind mount puts that directory at `/run/android-bridge` in a container, where
  the client looks by default.
- Every connection gets `/system/bin/sh` as the shell user, uid 2000, in
  `u:r:shell:s0`, with adbd's groups and adbd's capability bounding set. That
  is what `adb shell` gives you, and it is never root. Run `su` in it for root,
  which works only if the root manager grants su to Shell.

## Who can connect

Only real root. Three checks, each one denies when it cannot tell:

1. **File permissions.** The directory is 0700 and the socket 0600, both owned
   by root. Any other uid gets `EACCES` from the kernel before the daemon sees
   anything.
2. **Peer uid.** The daemon reads the peer's credentials from the kernel and
   wants uid 0 as the host sees it. Root inside a user namespace, such as
   rootless podman or `unshare -r`, shows up as the host uid it maps to.
3. **User namespace.** The peer must share the daemon's user namespace. This
   catches a process that real root mapped to 0 inside a child namespace,
   which has uid 0 but no power over the host.

Droidspaces containers do not use user namespaces, so container root is host
uid 0 and passes. Every container that gets the socket can open a
shell on the phone. Do not give it to a container that runs untrusted code as
root.

## Install

1. Build the module with `make module`, or take the zip from a release.
2. Flash `android-bridge-module-v*.zip` in the root manager and reboot.
3. In Droidspaces, give each container you want to reach the phone a read-only bind
   mount from `/dev/android-bridge` to `/run/android-bridge`, then restart it. On the
   command line that is `--bind=/dev/android-bridge:/run/android-bridge:ro`. The
   directory is mounted rather than the socket, so a restarted daemon is still
   reachable.
4. Put the client in the container. It is the same static binary as the
   daemon, `/data/adb/modules/android-bridge/bin/android-bridge` on the phone, or
   `bin/arm64/android-bridge` inside the zip for an aarch64 phone. The zip uses
   the root manager's arch names: `arm64`, `arm`, `x64`, `x86` and `riscv64`.

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

Arguments are joined with spaces and run with `sh -c`, the same as `adb shell`,
so quote the same way. A terminal is allocated for an interactive shell on a
terminal. `-t` forces one, `-T` refuses one. The exit code is the remote
command's, or 255 when the bridge itself failed.

## Logs

`/data/adb/android-bridge.log`, with the previous boot in `.old`. One line per
connection: peer pid, host or container, mode, and exit code. Commands are not
logged.

## Build

The Makefile is the Droidspaces one, ported. Static musl builds, per-arch
objects under `output/.obj/<arch>`, and incremental rebuilds. Toolchains are
found through `MUSL_CROSS` or `~/toolchains`. `scripts/install-musl.sh <arch>`
installs one.

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

## Status

Tested on Linux: the smoke test, a simulated container with its own mount and
PID namespaces, and AddressSanitizer runs with malformed and random frames.
Not yet tested on a phone. The SELinux side is the part most likely to need
work. If the shell refuses to start, the log says why, and `sepolicy.rule` is
where the fix goes. The module installer was checked on Linux against a mock of
the manager's install functions on 2026-10-31, not yet in KernelSU, Magisk or
APatch.

## Protocol

Frames of one type byte, a 4 byte big-endian length and the payload. The
client sends a hello with the protocol version, flags, window size, `TERM`
and the command, then stdin, stdin EOF and window changes. The daemon sends
stdout, stderr and a final exit code. Both ends buffer without blocking, so a
shell that stops reading cannot wedge the other direction.
