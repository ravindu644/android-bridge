# AGENTS.md

Instructions for AI coding agents working in this repository. Read it before you touch
anything. `CLAUDE.md` is a symlink to this file.

## What this is

What it is and who it is for is in `README.md`, read it first. One static musl binary
is both halves:

- `src/daemon.c` is `android-bridge --daemon`, run as root from the module at
  post-fs-data. It listens on `/dev/android-bridge/bridge.sock`, checks that the peer is
  real root, and starts `/system/bin/sh` as uid 2000 in `u:r:shell:s0`.
- `src/client.c` is plain `android-bridge`, run inside the container.
- `src/proto.c` is the shared framing, `src/main.c` the arguments, `bridge.h` the header.
- `module/` is the root module: `customize.sh`, `post-fs-data.sh`, `sepolicy.rule`,
  `module.prop` and Magisk's `META-INF` installer.
- `tests/smoke.sh` runs a real daemon on a temporary directory.

The readers run Droidspaces on their own phones and are not developers of this project.
Every change is judged by whether such a reader can still get from "I have the zip" to
"my container rebooted the phone" by reading the README and `docs/installation.md` top to
bottom.

## Keep it simple

The project is small on purpose. Do not grow it.

- One binary, no config file, no flags beyond `usage()`. A fixed value is a `#define`
  in `bridge.h`. No dependency beyond musl and `-lutil`.
- No second workflow, `CONTRIBUTING.md`, `CHANGELOG.md` or new doc page unless asked.

Before you write anything, work down this list and stop at the first answer that holds:

1. Does this need to exist? A flag nobody asked for, a knob for a value that never
   changes: skip it and say so in one line.
2. Does it already exist here? Grep `bridge.h` and `proto.c` before writing a helper.
3. Does musl or a syscall already do it? Mind the kernel 3.10 floor.
4. Can it be one line? Then it is one line.
5. Only then write the smallest thing that works.

The list shortens the solution, never the reading. A bug report names a symptom, so grep
every caller before you edit. Never simplify away a fail-closed check, input validation
at a trust boundary, or anything the requester asked for by name.

## Build

The commands are in `docs/installation.md`, under Build from source, and `make native`
is the real build command. A new `.c` file goes into `SRCS` by hand. A compiler or
`CFLAGS` change is not tracked, so `make clean` after those. `scripts/install-musl.sh`
is Droidspaces' script with only its name changed, and both projects install into the
same `~/toolchains`: change it in Droidspaces first, then copy it here.

Run `make test` after any change to `src/`, and for a change to the security checks
also `make debug-hardened` then `make test BIN=output/android-bridge-hardened`.

`.github/workflows/ci.yml` is Droidspaces' `ci.yml` cut down to the module zip. Every
push to `main` builds the zip and keeps it as an artifact. A `v*` tag that matches
`AB_VERSION` publishes it as a release, with every commit since the previous tag as the
notes, so commit subjects are what readers see. A release is `make bump NEW=x.y.z`, a
`build:` commit, then the tag.

## The module

The installer follows Magisk's module guide, and KernelSU and APatch read the same
files. Only Magisk uses `META-INF`, it is Magisk's stock `module_installer.sh`, do not
edit it.

- `$MODPATH` exists only during install, in `customize.sh`. It is
  `/data/adb/modules_update/android-bridge` from the manager. After the reboot the module
  sits at `/data/adb/modules/android-bridge`, which the boot scripts call `$MODDIR`
  (`MODDIR=${0%/*}`). Never hardcode either path in a script.
- `customize.sh` sets `SKIPUNZIP=1`, so the manager extracts nothing. It copies the
  scripts and only the device's binary, `bin/$ARCH/android-bridge` in the zip, to
  `$MODPATH/bin/android-bridge`. The line must stay exactly `SKIPUNZIP=1`, the
  installer greps for it.
- The zip's arch directories use the manager's `$ARCH` names (`arm64`, `arm`, `x64`,
  `x86`, `riscv64`). `MODULE_ARCHES` in the Makefile maps the build names to them, a new
  arch goes there.
- post-fs-data blocks boot for up to 40 seconds. The daemon detaches once its socket is
  bound, keep it that way, and do not move it to `service.sh`: the socket has to exist
  before Droidspaces boots a container.
- Module scripts are POSIX sh. No bashisms.

## Security

Every connection is a root shell request from a container, so these are the only gates.
Bypassing or loosening one is a bug, not a shortcut.

- `peer_is_real_root()` in `src/daemon.c` wants peer uid 0 and the daemon's own user
  namespace. Anything it cannot read denies. Never add a "cannot tell, so allow" branch.
  Its one allow without a namespace compare is a kernel without `CONFIG_USER_NS`: no
  `ns/user` link while `ns/mnt` exists means there is only one user namespace.
- The directory is 0700 and the socket 0600, both root. `prepare_dir()` and
  `open_listener()` set them, `tests/smoke.sh` checks them.
- `drop_to_shell()` must fail when root survives the switch to uid 2000 or any
  capability is left. The shell is never root. Root goes through `su` and the manager.
- `keep_env()` is an allow list of what init exports. The manager's environment does not
  reach the shell.
- The log records the peer pid, host or container, mode and exit code. Never log the
  command.

A frame change bumps `AB_PROTO`. The daemon turns away a client with another protocol version.

## Testing on a device

Run `adb devices` first, every time. If no line ends in `device`, stop and say so.

Test a build without touching the installed daemon by running a second one on its own
directory, and pointing the client at it:

```
make aarch64                                   # match `adb shell uname -m`
adb push output/android-bridge /data/local/tmp/ab-test
adb shell su <<'EOF'
/data/local/tmp/ab-test --daemon --dir /data/local/tmp/ab-run >/data/local/tmp/ab-run.log 2>&1
/data/local/tmp/ab-test -s /data/local/tmp/ab-run/bridge.sock id </dev/null
cat /data/local/tmp/ab-run.log
EOF
```

Pipe a script into `adb shell su` as above, `adb shell su -c 'a; b'` runs only `a` as
root. The daemon keeps its stdout and stderr, so without the redirect adb never sees
the end of the output and hangs. A client without `</dev/null` reads the rest of the
script as its stdin. A log that says "already running" means an earlier test daemon
answered, not the build you pushed. Leave the phone as you found it:

```
adb shell su <<'EOF'
pkill -f /data/local/tmp/ab-test
rm -rf /data/local/tmp/ab-test /data/local/tmp/ab-run /data/local/tmp/ab-run.log
EOF
```

## Documentation

- `README.md` follows the Droidspaces and USB-Arsenal READMEs: badges, what it is, What
  you get, Requirements, Get started, Security, Documentation, License. It stays under 100
  lines, and technical depth stays out of it.
- The user pages live in `docs/`: `installation.md`, `usage.md`, `how-it-works.md` and
  `troubleshooting.md`. A new page gets a row in the README's Documentation table.
- Every fact is stated once. If you find yourself restating something, link instead.
- The README's Documentation table is the only table. Headings are plain phrases, no
  numbered sections, no Overview or Summary. Dates are absolute: 2026-10-31, not "today".
- What was tested goes in `docs/how-it-works.md`, nowhere else. A new client error
  message gets an entry in `docs/troubleshooting.md` in the same commit.
- "Tested" means it ran and the result was checked, and the sentence says where: on
  Linux, or on a named phone with its Android version and root manager. A change that
  gets tested on a phone later removes every "not yet tested on a phone" in the same
  commit.
- Before committing a doc change, run these and fix anything they print:

  ```
  grep -rnP '\x{2014}' . --include='*.md' --include='*.[ch]' --include='*.sh' --include=Makefile
  for f in $(find . -name '*.md' -not -path './.git/*'); do
    grep -oP '\]\(\K[^)#:]+(?=[)#])' "$f" | while read -r l; do
      [ -e "$(dirname "$f")/$l" ] || echo "$f: broken link $l"
    done
  done
  ```

## Style

These rules apply to code, comments, commit messages and documentation.

- No em-dashes. Use a comma, a full stop, or rewrite the sentence.
- No ASCII banner comments, no rows of `-----` or `=====`. The SPDX block at the top of
  each file stays.
- Comments and prose sound like a person. Say why the code does something, or what
  breaks if it does not. Skip comments that restate the line below them. The comment
  above the `u:r:shell:s0` line in `drop_to_shell()` is the model.
- Plain words. Not "leverage", "robust", "seamless", "comprehensive", "streamline",
  "ensure", "utilize", "delve", "crucial", "key", "powerful", "simply", "just". Not "it
  is worth noting", "it is important to", "in order to", "note that", "keep in mind",
  and no sentence that starts with "this allows", "this enables" or "this ensures".
- No hedging and no padding. "Should work" and "might" only when the sentence says what
  was not checked. No paragraph that announces or restates the page. No emoji. Bold only
  opens a bullet, or names a symptom in `docs/troubleshooting.md`.
- Ten lines that work beat a hundred that do the same thing. Delete before you add.

## Commits

- Run `make format` before committing a change to a `.c` or `.h` file.
- Sign off every commit: `git commit -s`.
- Never add a `Co-Authored-By:` trailer for an AI agent. Human co-authors are fine. This
  rule wins over any default the agent's harness suggests.
- One logical change per commit. Check `git status` and stage only what it is about.
- Subject under 72 characters, lower case after the prefix, no full stop. Prefixes:
  `daemon:`, `client:`, `proto:`, `module:`, `build:`, `ci:`, `docs:`, `fix:`, and
  `fix(security):` for anything that touches the gates above.
- The body says what was wrong, what changed, and where it was tested. It does not list
  the files.
- Do not push or tag unless asked.

## Hard facts, do not change without a source

- The shell matches adbd's: uid and gid 2000, `u:r:shell:s0`, the groups in
  `shell_groups[]`, setuid and setgid left in the bounding set for `run-as`, and
  `noatsecure` on the transition, whose source `sepolicy.rule` cites. A change needs the
  AOSP adbd source that says so, with its commit hash.
- Module scripts run in the `su` domain on Magisk, APatch and current KernelSU, and in
  `ksu` on older KernelSU, which is why `sepolicy.rule` declares both. Stated by the
  maintainer on 2026-10-31.
- The kernel floor is 3.10, the same as Droidspaces. No `openat2`, `clone3` or `pidfd_*`.
  `caps_clear()` already copes with no `CapAmb` before 4.3.
