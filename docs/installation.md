# Installation

android-bridge is one static binary with two jobs. On the phone the module runs it at
boot as the bridge. In a container you run it as the client. A container reaches the
bridge through a bind mount you add in Droidspaces.

## The module

1. Take `android-bridge-module-v*.zip` from the
   [latest release](https://github.com/ravindu644/android-bridge/releases/latest), or
   build it, see [build from source](#build-from-source).
2. Flash it in KernelSU, Magisk or APatch, and reboot.
3. Check that the bridge started. In a root shell on the phone:

   ```
   cat /data/adb/android-bridge.log
   ```

   The first line contains `listening on /dev/android-bridge/bridge.sock`. Without a root
   shell on the phone, skip this: the client tells you from the container if the bridge
   is missing.

What the module does:

- **Installs the phone's binary** as `/data/adb/modules/android-bridge/bin/android-bridge`.
  The zip carries every architecture, and only the one your phone needs is installed.
- **Loads `sepolicy.rule` at boot,** which lets the bridge start the shell in the same
  SELinux domain `adb shell` uses.
- **Starts the bridge early in boot,** before Droidspaces starts any container, and logs
  to `/data/adb/android-bridge.log`.

Nothing else runs. Outside the module, the bridge creates `/dev/android-bridge` at each
boot and keeps the log of the previous boot as `android-bridge.log.old`.

## Give a container the bridge

The bridge listens in `/dev/android-bridge` on the phone. The container needs that
folder at `/run/android-bridge`, read-only, which is where the client looks.

In the Droidspaces app:

1. On the **Containers** tab, press and hold the container's card and open **Edit
   Container Configuration**.
2. Under **Advanced Options**, open **Bind Mounts** and tap **Add Bind Mount**.
3. For **Folder on the phone**, choose `/dev/android-bridge`. For **Path in the
   container**, type `/run/android-bridge`. Turn on **Read-only** and tap **Add**.
4. Save your changes, then restart the container. Bind mounts apply at the next start.

On the command line, add this to `droidspaces start` or `restart`:

```
--bind=/dev/android-bridge:/run/android-bridge:ro
```

or this line to the container's config file:

```
bind_mounts=/dev/android-bridge:/run/android-bridge:ro
```

The folder is mounted rather than the socket inside it, so the container still reaches
the bridge after the bridge restarts.

Do this only for containers you trust, see [Security](../README.md#security).

## Copy the client into the container

The client is the same binary as the bridge. Inside the zip it is
`bin/<arch>/android-bridge`, with the root manager's names for the architecture. Run
`uname -m` in the container to pick one:

- `aarch64` is `arm64`, which most phones are.
- `armv7l` or `armv8l` is `arm`.
- `x86_64` is `x64`, `i686` is `x86`, and `riscv64` is `riscv64`.

With **Android Storage** turned on for the container and the zip in your Download
folder, as root in the container, for an `arm64` phone. Ubuntu and Debian images come
without `unzip`, so install it first with `apt install unzip`.

```
unzip -j /storage/emulated/0/Download/android-bridge-module-v*.zip \
  bin/arm64/android-bridge -d /usr/local/bin
chmod 755 /usr/local/bin/android-bridge
```

On the phone, the installed copy is `/data/adb/modules/android-bridge/bin/android-bridge`,
if you would rather copy it from a root shell there.

After you update the module, copy the client again.

## Build from source

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

`make test` runs a real bridge on a temporary directory. It needs root and
`/system/bin/sh`, so link that to `/bin/sh` on a machine where that is fine.
`make test BIN=output/android-bridge-hardened` runs it under the sanitizers.

## Uninstall

Remove the module in the root manager and reboot. Then remove the bind mount from each
container, and `/usr/local/bin/android-bridge` inside it.
