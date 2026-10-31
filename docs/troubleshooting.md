# Troubleshooting

Each message the client can print, what it means and the fix. The bridge's own side of
the story is in its log, `/data/adb/android-bridge.log` on the phone, see
[usage](usage.md#the-log).

**`android-bridge: permission denied, only root may use the bridge`**
You ran it as a normal user, or as root in a rootless container such as rootless podman,
which is a normal user on the phone. The kernel only lets root open the socket. Run it as
root, or with `sudo`, in the Droidspaces container itself.

**`android-bridge: permission denied, only real root may connect`**
You are root inside a user namespace, such as one made with `unshare -r`. That root is
not root on the phone, so the bridge refuses it. Run the client from the
Droidspaces container itself. The log has a `denied pid ... uid 0` line for it.

**`android-bridge: no bridge at /dev/android-bridge/bridge.sock`**, followed by
**`Is the daemon running, and is /dev/android-bridge bind-mounted at /run/android-bridge?`**
The container cannot see the bridge. When `/run/android-bridge/bridge.sock` is missing,
the client tries the phone's own path, and that is missing in a container too. Check, in
this order:

1. `ls /run/android-bridge` in the container. Empty or missing means the bind mount is
   not there. Add it as in [installation](installation.md#give-a-container-the-bridge),
   and restart the container, a running one does not pick it up.
2. Whether the bridge was running when the container started. Droidspaces skips a bind
   mount whose folder is missing at start, so a container that started before the
   bridge, or while it was not running, gets nothing. Restart the container.
3. The log on the phone. No `listening on` line since the last boot means the bridge did
   not start. Check that the module is installed and enabled, and reboot.

**`android-bridge: the daemon is not running (/run/android-bridge/bridge.sock)`**
The socket is there but nothing answers it, so the bridge stopped after it started. The
end of the log says why. Reboot the phone to start it again.

**`android-bridge: the client and the daemon are different versions`**
The client in the container and the module on the phone come from releases that do not
speak to each other. Copy the client from the installed module again, see
[copy the client](installation.md#copy-the-client-into-the-container).

**`android-bridge: cannot start a shell`**
The bridge accepted you but could not set up the shell. The log has a `cannot start a
shell` line with the reason.

**`android-bridge: cannot run /system/bin/sh: Permission denied`**
The module's SELinux rules did not load, so the switch into `u:r:shell:s0` was refused.
Reinstall the module from the root manager and reboot. The command exits with 127, and
nothing ran.

**`android-bridge: cannot set the SELinux exec context to u:r:shell:s0`,
`cannot drop capabilities`, `cannot switch to the shell user`, `privileges survived the
switch to the shell user` or `cannot chdir to /`**
The phone would not let the bridge turn into the shell user. It refuses to hand out a
shell that is still privileged, so nothing ran and the command exits with 126. Open an
issue with the phone, Android version, root manager and the log.

**`android-bridge: lost the connection to the daemon`**
The connection closed before the shell sent its exit code. The bridge stopped, or it
refused the connection and closed it before its message arrived. Look for a `denied`
line near the end of the log.

**`android-bridge: protocol error`**
The bridge sent something the client did not understand, which happens when the client
and the module come from different builds. Copy the client from the installed module
again.

**`android-bridge: /run/android-bridge/bridge.sock:` followed by another error**
Connecting failed for a reason the client has no advice for, such as a socket path that
is not a socket. Check the bind mount as above, then the log.

**`android-bridge: command too long`**
The command line is longer than about 64 KB. Put the command in a script on the phone
and run that.

**`su` in the bridge shell says permission denied.**
The root manager has not granted su to Shell. Grant it in the manager, the same as for
`adb shell`.
