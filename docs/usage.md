# Usage

Run the client as root inside a container that has the bind mount, see
[installation](installation.md#give-a-container-the-bridge).

```
android-bridge                         # interactive shell, like adb shell
android-bridge reboot                  # reboot the phone
android-bridge dumpsys battery
echo hello | android-bridge cat        # pipes and exit codes pass through
android-bridge -t top                  # force a terminal for a command
android-bridge su -c /data/local/Droidspaces/bin/droidspaces show
```

## Commands and quoting

Arguments are joined with spaces and run with `sh -c` on the phone, the same as
`adb shell`, so quote the same way. This runs the pipe on the phone:

```
android-bridge 'logcat -d | grep -i battery'
```

and this runs `grep` in the container:

```
android-bridge logcat -d | grep -i battery
```

## Options

- **`-t`** always gives the command a terminal, for programs like `top` that want one.
- **`-T`** never gives it one, for output you pipe or save.
- **`-s SOCKET`** connects to another socket. The default is
  `/run/android-bridge/bridge.sock`, or `/dev/android-bridge/bridge.sock` when that is
  missing, so the client also works from a root shell on the phone.
- **`-h`** prints the help, **`-V`** the version.

Without `-t` or `-T`, an interactive shell gets a terminal when you run it from one, and
a command gets none.

## Root

The shell is the shell user, uid 2000, the same as `adb shell`. Run `su` in it for root.
That works only if your root manager grants su to Shell, and the manager asks or decides
the same way it does over ADB.

## Exit codes

The exit code is the remote command's, so `sh` on the phone gives 127 for a command it
cannot find and 126 for one it cannot run, as it does over `adb shell`. The bridge adds
its own failures to these codes:

- **255:** a bad option, the client could not reach the bridge, the client and the
  bridge are different versions, or the bridge could not start a shell.
- **126:** the switch to the shell user failed, and the bridge refused to run anything.
- **127:** `/system/bin/sh` itself could not run.

Each of these comes with an `android-bridge:` message on stderr, see
[troubleshooting](troubleshooting.md).

## The log

The bridge logs to `/data/adb/android-bridge.log` on the phone, in UTC. The log of the
previous boot is kept as `android-bridge.log.old`.

Each connection logs the client's pid, whether it came from the host or a container and
whether it got a terminal, then the exit code:

```
[2026-10-31 09:36:23] android-bridge 0.1.0 listening on /dev/android-bridge/bridge.sock, pid 909
[2026-10-31 09:40:45] pid 20575 (container): interactive pty, shell pid 20577
[2026-10-31 09:41:12] shell pid 20577: exit 0
```

Commands are never logged.
