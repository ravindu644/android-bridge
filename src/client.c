/*
 * android-bridge: the container side
 *
 * Copyright (C) 2026 ravindu644 <droidcasts@protonmail.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "bridge.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static struct buf tosock, in;
static struct termios saved;
static int raw_on;
static int in_fd; /* what goes to the remote stdin, -1 for nothing */

/* push and pull are a normal session running cat, so the daemon and the
 * protocol stay as they are. The client only counts the bytes. */
static struct {
  int on, tty, drawn; /* drawn: the bar is on screen, its line not ended */
  int out;            /* pull: the local file, -1 until stat's line is in */
  const char *local;  /* pull: where to write */
  const char *name;   /* shown on the progress line */
  long long done, total;
  double start, shown;
  char line[24];
  size_t llen;
} xf = {.out = -1};

static void restore_tty(void) {
  if (raw_on)
    tcsetattr(0, TCSADRAIN, &saved);
  raw_on = 0;
}

static void on_fatal(int sig) {
  restore_tty();
  signal(sig, SIG_DFL);
  raise(sig);
}

static int connect_bridge(const char *path) {
  struct sockaddr_un a = {.sun_family = AF_UNIX};
  if (strlen(path) >= sizeof(a.sun_path)) {
    fprintf(stderr, "android-bridge: socket path too long: %s\n", path);
    return -1;
  }
  strcpy(a.sun_path, path);
  int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (s >= 0 && connect(s, (struct sockaddr *)&a, sizeof(a)) == 0)
    return s;

  int e = errno;
  if (e == EACCES || e == EPERM)
    fprintf(stderr, "android-bridge: permission denied, only root may use "
                    "the bridge\n");
  else if (e == ENOENT)
    fprintf(stderr,
            "android-bridge: no bridge at %s\n"
            "Is the daemon running, and is " AB_HOST_DIR
            " bind-mounted at " AB_CONTAINER_DIR "?\n",
            path);
  else if (e == ECONNREFUSED)
    fprintf(stderr, "android-bridge: the daemon is not running (%s)\n", path);
  else
    fprintf(stderr, "android-bridge: %s: %s\n", path, strerror(e));
  return -1;
}

static void get_size(uint16_t *rows, uint16_t *cols) {
  struct winsize ws = {0};
  if (ioctl(0, TIOCGWINSZ, &ws) < 0)
    ioctl(1, TIOCGWINSZ, &ws);
  *rows = ws.ws_row;
  *cols = ws.ws_col;
}

/* adb shell joins its arguments with spaces and hands them to sh -c, so
 * quoting works the same way here as people already expect from adb. */
static char *join(int argc, char **argv) {
  size_t len = 1;
  for (int i = 0; i < argc; i++)
    len += strlen(argv[i]) + 1;
  char *s = calloc(1, len);
  if (!s)
    return NULL;
  for (int i = 0; i < argc; i++) {
    if (i)
      strcat(s, " ");
    strcat(s, argv[i]);
  }
  return s;
}

static int send_hello(int pty, const char *cmd) {
  const char *term = getenv("TERM");
  size_t tlen = term ? strlen(term) : 0, clen = strlen(cmd);
  if (tlen > 63)
    tlen = 0; /* no real terminal type is this long */
  size_t len = 7 + tlen + clen;
  if (len > FRAME_MAX) {
    fprintf(stderr, "android-bridge: command too long\n");
    return -1;
  }
  unsigned char *p = malloc(len);
  if (!p)
    return -1;
  uint16_t rows, cols;
  get_size(&rows, &cols);
  p[0] = AB_PROTO;
  p[1] = pty ? HELLO_PTY : 0;
  put_be16(p + 2, rows);
  put_be16(p + 4, cols);
  p[6] = (unsigned char)tlen;
  memcpy(p + 7, term, tlen);
  memcpy(p + 7 + tlen, cmd, clen);
  buf_frame(&tosock, F_HELLO, p, (uint32_t)len);
  free(p);
  return 0;
}

static double now(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void human(char o[32], double v) {
  static const char *const unit[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  int i = 0;
  while (v >= 1024 && i < 4) {
    v /= 1024;
    i++;
  }
  unsigned t = (unsigned)(v * 10);
  if (i)
    snprintf(o, 32, "%u.%u %s", t / 10, t % 10, unit[i]);
  else
    snprintf(o, 32, "%u B", t / 10);
}

/* Anything else written to stderr starts on a line of its own. */
static void endbar(void) {
  if (xf.drawn)
    fputc('\n', stderr);
  xf.drawn = 0;
}

/* Redrawn at most four times a second, and only on a terminal. The last line
 * is printed either way, the way adb prints its own. */
static void show(int last) {
  double t = now(), dt = t - xf.start;
  if (!last && (!xf.tty || t - xf.shown < 0.25))
    return;
  xf.shown = t;
  xf.drawn = !last && xf.tty;
  char got[32], rate[32];
  human(got, (double)xf.done);
  human(rate, dt > 0 ? (double)xf.done / dt : 0);
  const char *cr = xf.tty ? "\r\033[K" : "";
  if (last) {
    fprintf(stderr, "%s%s: %s in %.1fs, %s/s\n", cr, xf.name, got, dt, rate);
  } else if (xf.total <= 0) {
    /* a pipe, or a /proc file that stat calls empty */
    fprintf(stderr, "%s%s %s %s/s", cr, xf.name, got, rate);
  } else {
    long long pct = xf.done >= xf.total ? 100 : xf.done * 100 / xf.total;
    long long eta =
        xf.done
            ? (long long)(dt * (double)(xf.total - xf.done) / (double)xf.done)
            : 0;
    if (eta < 0)
      eta = 0;
    fprintf(stderr, "%s%s %3lld%% [%-30.*s] %s %s/s ETA %lld:%02lld", cr,
            xf.name, pct, (int)(pct * 30 / 100),
            "==============================", got, rate, eta / 60, eta % 60);
  }
}

/* The remote side sends stat's size on a line of its own, then the file. It
 * sends the line only once the file is open for reading, and the local file is
 * opened only after that line, so a remote file that is missing or unreadable
 * does not truncate a local one that is already there. */
static int pull_data(const unsigned char *p, uint32_t n) {
  for (; xf.out < 0 && n; p++, n--) {
    if (*p != '\n') {
      if (xf.llen == sizeof(xf.line) - 1)
        goto bad;
      xf.line[xf.llen++] = (char)*p;
      continue;
    }
    char *end;
    xf.total = strtoll(xf.line, &end, 10);
    if (!xf.llen || *end)
      goto bad;
    xf.out = open(xf.local, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (xf.out < 0)
      goto local;
  }
  if (n && write_all(xf.out, p, n) < 0)
    goto local;
  xf.done += n;
  show(0);
  return 0;
local:
  endbar();
  fprintf(stderr, "android-bridge: %s: %s\n", xf.local, strerror(errno));
  return -1;
bad:
  endbar();
  fprintf(stderr, "android-bridge: unexpected reply from stat on the phone\n");
  return -1;
}

static int session(const char *path, int pty, const char *cmd) {
  int s = connect_bridge(path);
  if (s < 0 || send_hello(pty, cmd) < 0)
    return 255;

  sigset_t winch;
  sigemptyset(&winch);
  sigaddset(&winch, SIGWINCH);
  sigprocmask(SIG_BLOCK, &winch, NULL);
  int sfd = signalfd(-1, &winch, SFD_CLOEXEC | SFD_NONBLOCK);

  if (pty && isatty(0) && tcgetattr(0, &saved) == 0) {
    struct termios t = saved;
    cfmakeraw(&t);
    tcsetattr(0, TCSADRAIN, &t);
    raw_on = 1;
    atexit(restore_tty);
    signal(SIGTERM, on_fatal);
    signal(SIGHUP, on_fatal);
    signal(SIGINT, on_fatal);
  }

  /* Only the socket goes non-blocking: O_NONBLOCK on a shared terminal would
   * leak into the shell that started us. Local writes may block, which
   * pushes back on the daemon, and it never blocks on us. */
  set_nonblock(s);
  int stdin_eof = in_fd < 0;
  if (stdin_eof)
    buf_frame(&tosock, F_STDIN_EOF, NULL, 0);
  for (;;) {
    uint8_t t;
    uint32_t len;
    int r;
    while ((r = frame_peek(&in, &t, &len)) == 1) {
      unsigned char *p = in.data + FRAME_HDR;
      if (t == F_STDOUT && xf.local) {
        if (pull_data(p, len) < 0)
          return 1;
      } else if (t == F_STDOUT)
        write_all(1, p, len);
      else if (t == F_STDERR) {
        endbar();
        write_all(2, p, len);
      } else if (t == F_EXIT && len == 4)
        return (int)(get_be32(p) & 0xff);
      else {
        r = -1;
        break;
      }
      buf_consume(&in, FRAME_HDR + len);
    }
    if (r < 0) {
      restore_tty();
      fprintf(stderr, "android-bridge: protocol error\n");
      return 255;
    }
    if (buf_flush(&tosock, s) < 0)
      break;

    struct pollfd pf[3] = {
        {s, (short)(POLLIN | (tosock.len ? POLLOUT : 0)), 0},
        {sfd, POLLIN, 0},
        {in_fd, 0, 0},
    };
    int n = 2;
    if (!stdin_eof && buf_room(&tosock) >= CHUNK + FRAME_HDR) {
      pf[2].events = POLLIN;
      n = 3;
    }
    if (poll(pf, (nfds_t)n, -1) < 0) {
      if (errno == EINTR)
        continue;
      break;
    }
    if (pf[0].revents & (POLLIN | POLLHUP | POLLERR)) {
      ssize_t rr = buf_read(&in, s);
      if (rr == 0 || rr == -1)
        break;
    }
    if (pf[1].revents & POLLIN) {
      struct signalfd_siginfo si;
      while (read(sfd, &si, sizeof(si)) > 0)
        ;
      unsigned char ws[4];
      uint16_t rows, cols;
      get_size(&rows, &cols);
      put_be16(ws, rows);
      put_be16(ws + 2, cols);
      if (pty)
        buf_frame(&tosock, F_WINCH, ws, 4);
    }
    if (n == 3 && pf[2].revents) {
      unsigned char tmp[CHUNK];
      ssize_t rd = read(in_fd, tmp, sizeof(tmp));
      if (rd > 0) {
        buf_frame(&tosock, F_STDIN, tmp, (uint32_t)rd);
        if (xf.on) {
          xf.done += rd;
          show(0);
        }
      } else if (rd == 0 || (errno != EINTR && errno != EAGAIN)) {
        stdin_eof = 1;
        buf_frame(&tosock, F_STDIN_EOF, NULL, 0);
      }
    }
  }
  restore_tty();
  fprintf(stderr, "android-bridge: lost the connection to the daemon\n");
  return 255;
}

/* pty_mode: -1 like adb (a terminal only for an interactive shell on a
 * terminal), 0 never, 1 always. */
int run_client(const char *path, int pty_mode, int argc, char **argv) {
  char *cmd = join(argc, argv);
  if (!cmd)
    return 255;
  int pty = pty_mode >= 0 ? pty_mode : !cmd[0] && isatty(0);
  int r = session(path, pty, cmd);
  free(cmd);
  return r;
}

/* For sh -c: inside single quotes only ' itself needs care, as '\''. */
static char *sq(const char *s) {
  char *o = malloc(strlen(s) * 4 + 3), *d = o;
  if (!o)
    return NULL;
  *d++ = '\'';
  for (; *s; s++) {
    if (*s == '\'') {
      memcpy(d, "'\\''", 4);
      d += 4;
    } else {
      *d++ = *s;
    }
  }
  *d++ = '\'';
  *d = 0;
  return o;
}

/* adb push LOCAL REMOTE and adb pull REMOTE [LOCAL], one file each. A
 * directory on the receiving side gets the file's name appended. */
int run_xfer(const char *path, int pull, int argc, char **argv) {
  if (argc != 2 && !(pull && argc == 1)) {
    fprintf(stderr, "Usage: android-bridge push LOCAL REMOTE\n"
                    "       android-bridge pull REMOTE [LOCAL]\n");
    return 255;
  }
  char *copy = strdup(argv[0]), *base = copy ? basename(copy) : NULL;
  char *q0 = sq(argv[0]), *qb = base ? sq(base) : NULL, *cmd = NULL;
  if (!qb || !q0)
    return 255;
  xf.name = base;

  if (pull) {
    struct stat st;
    xf.local = argc == 2 ? argv[1] : base;
    if (stat(xf.local, &st) == 0 && S_ISDIR(st.st_mode)) {
      char *l;
      if (asprintf(&l, "%s/%s", xf.local, base) < 0)
        return 255;
      xf.local = l;
    }
    in_fd = -1;
    /* The redirect fails before anything is printed. Without toybox's stat,
     * on Android before 6.0, the size is 0 and there is no bar. */
    if (asprintf(&cmd,
                 "[ -d %s ] && { echo %s: is a directory >&2; exit 1; }; "
                 "{ stat -Lc %%s -- %s 2>/dev/null || echo 0; cat; } < %s",
                 q0, q0, q0, q0) < 0)
      return 255;
  } else {
    struct stat st;
    in_fd = open(argv[0], O_RDONLY | O_CLOEXEC);
    if (in_fd < 0 || fstat(in_fd, &st) < 0) {
      fprintf(stderr, "android-bridge: %s: %s\n", argv[0], strerror(errno));
      return 1;
    }
    if (S_ISDIR(st.st_mode)) {
      fprintf(stderr, "android-bridge: %s: is a directory\n", argv[0]);
      return 1;
    }
    xf.total = S_ISREG(st.st_mode) ? st.st_size : 0;
    char *q1 = sq(argv[1]);
    if (!q1 ||
        asprintf(&cmd, "d=%s; [ -d \"$d\" ] && d=\"$d\"/%s; cat > \"$d\"", q1,
                 qb) < 0)
      return 255;
  }

  xf.on = 1;
  xf.tty = isatty(2);
  xf.start = now();
  int r = session(path, 0, cmd);
  /* close() is where NFS and FUSE report a write that did not land. */
  if (r == 0 && xf.out >= 0 && close(xf.out) < 0) {
    endbar();
    fprintf(stderr, "android-bridge: %s: %s\n", xf.local, strerror(errno));
    r = 1;
  }
  if (r == 0)
    show(1);
  endbar();
  return r;
}
