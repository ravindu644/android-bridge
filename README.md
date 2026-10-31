[![Latest release](https://img.shields.io/github/v/release/ravindu644/android-bridge?label=Release&style=for-the-badge)](https://github.com/ravindu644/android-bridge/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](./LICENSE)

# android-bridge

android-bridge lets a [Droidspaces](https://github.com/ravindu644/Droidspaces-OSS)
container reach the phone it runs on. Root inside the container gets a shell on Android,
the same one `adb shell` gives you, with no cable and no ADB.

On a homelab phone nobody touches, that is how a container reboots the phone, runs `pm`,
`cmd` or `dumpsys`, or manages Droidspaces itself. `reboot` inside a container only
reboots the container.

## What you get

- **adb shell, from inside the container.** `android-bridge` opens an interactive shell
  on the phone, `android-bridge reboot` runs one command. Pipes, terminals and exit codes
  pass through.
- **Root when you allow it.** The shell is never root. Run `su` in it, and your root
  manager decides, the same as over ADB.
- **Only root gets in.** A normal user in the container is refused, and so is root inside
  a user namespace, such as rootless podman.
- **Ready at boot.** The module starts the bridge before Droidspaces boots any container,
  so a container that starts at boot can use it straight away.
- **One small binary.** The same static binary is the bridge on the phone and the client
  in the container, for arm64, arm, x86_64, x86 and riscv64.

## Requirements

- A rooted phone with KernelSU, Magisk or APatch.
- [Droidspaces](https://github.com/ravindu644/Droidspaces-OSS) and a container that should
  reach the phone.
- For `su` inside the bridge shell, a root manager that grants su to Shell.

## Get started

1. Flash `android-bridge-module-v*.zip` from the
   [latest release](https://github.com/ravindu644/android-bridge/releases/latest) in your
   root manager and reboot.
2. In Droidspaces, give the container a read-only bind mount from `/dev/android-bridge` to
   `/run/android-bridge`, then restart it. The
   [installation guide](docs/installation.md#give-a-container-the-bridge) shows where that
   is in the app.
3. Copy the client into the container, see
   [copy the client](docs/installation.md#copy-the-client-into-the-container).
4. Inside the container, as root:

   ```
   android-bridge                         # a shell on the phone, like adb shell
   android-bridge reboot                  # reboot the phone
   android-bridge dumpsys battery
   ```

Everything else the client does is in [usage](docs/usage.md).

## Security

> [!WARNING]
>
> android-bridge gives root in a container a shell on the phone.
>
> Every container you give the bind mount to can open one, so do not give it to a
> container that runs code you do not trust as root.
>
> If your root manager grants su to Shell, the bridge shell is one `su` away from root
> on the phone.

## Documentation

| Page | What it covers |
|------|----------------|
| [Installation](docs/installation.md) | The module, the bind mount in Droidspaces, the client, building from source. |
| [Usage](docs/usage.md) | Every option, quoting, exit codes and the log. |
| [How it works](docs/how-it-works.md) | Who can connect, the shell you get, and what was tested. |
| [Troubleshooting](docs/troubleshooting.md) | Each error message, what it means and the fix. |

## License

[GNU General Public License v3.0](./LICENSE). Copyright (C) 2026
[ravindu644](https://github.com/ravindu644).
