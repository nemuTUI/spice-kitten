/*
 * TTY input backend: reads keyboard and mouse events directly from the
 * terminal using the kitty keyboard protocol and SGR mouse reports.
 */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <time.h>

#include <libspice.h>

#include "input.h"
#include "tty_parse.h"

/* Push kitty keyboard protocol flags: disambiguate escape codes (0b1),
 * report event types (0b10), report alternate keys (0b100, gives the
 * base layout key for non-latin layouts) and report all keys as escape
 * codes (0b1000, gives press/release events for every key); popped on
 * exit to restore the previous terminal state */
#define KBD_MODE_PUSH "\x1b[>15u"
#define KBD_MODE_POP  "\x1b[<u"

/* any-motion tracking + SGR (decimal) coordinate encoding */
#define MOUSE_ON  "\x1b[?1003h\x1b[?1006h"
#define MOUSE_OFF "\x1b[?1003l\x1b[?1006l"

#define READ_BUF_LEN 4096
#define INPUT_FLUSH_MS 25

static struct termios orig_termios;
static bool termios_saved = false;

/* restore the terminal state exactly once, on any exit path */
static void tty_restore(void)
{
    if (!termios_saved) {
        return;
    }
    termios_saved = false;

    fputs(KBD_MODE_POP MOUSE_OFF, stdout);
    fflush(stdout);

    /* let the terminal apply the new modes and stop sending reports,
     * then drop the reports already queued for input: otherwise they
     * are echoed as garbage once ECHO is back on */
    usleep(100000);
    tcflush(STDIN_FILENO, TCIFLUSH);

    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
}

/* A dead VM can kill the process with SIGPIPE mid-send: restore the
 * terminal before dying, otherwise it is left in raw mode and looks
 * like a hung application */
static void tty_signal_handler(int sig)
{
    if (termios_saved) {
        static const char modes_off[] = KBD_MODE_POP MOUSE_OFF;

        termios_saved = false;
        write(STDOUT_FILENO, modes_off, sizeof(modes_off) - 1);
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        tcflush(STDIN_FILENO, TCIFLUSH);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static uint64_t now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Left-side modifier keys used to synthesize combinations: a terminal
 * without full kitty protocol support reports Ctrl+X or Shift+A as a
 * single event, so the modifiers are pressed and released around the
 * key tap */
static const struct {
    unsigned int mod;
    uint32_t keycode;
} tty_mod_keys[] = {
    { TTY_MOD_CTRL,  29 },  /* Left Control */
    { TTY_MOD_ALT,   56 },  /* Left Alt */
    { TTY_MOD_SHIFT, 42 },  /* Left Shift */
    { TTY_MOD_SUPER, 125 }, /* Left Super */
};

static void send_mods(spice_t *spice, unsigned int mods, bool down)
{
    for (size_t i = 0; i < sizeof(tty_mod_keys) / sizeof(tty_mod_keys[0]); i++) {
        if (mods & tty_mod_keys[i].mod) {
            if (down) {
                spice_send_key_press(spice, tty_mod_keys[i].keycode, 0);
            } else {
                spice_send_key_release(spice, tty_mod_keys[i].keycode, 0);
            }
        }
    }
}

/* Returns true when the quit hotkey was pressed */
static bool handle_key(spice_t *spice, const tty_event_t *ev)
{
    if (ev->keycode == 0) {
        return false; /* unmappable key */
    }

    if ((ev->mods & TTY_MOD_CTRL) && ev->keycode == 16) { /* Ctrl+Q */
        spice_cancel(spice);
        return true;
    }

    switch (ev->event) {
    case TTY_EV_RELEASE:
        spice_send_key_release(spice, ev->keycode, ev->keysym);
        break;
    case TTY_EV_TAP:
        if (ev->mods) {
            send_mods(spice, ev->mods, true);
        }
        spice_send_key_press(spice, ev->keycode, ev->keysym);
        spice_send_key_release(spice, ev->keycode, ev->keysym);
        if (ev->mods) {
            send_mods(spice, ev->mods, false);
        }
        break;
    default: /* press or repeat */
        spice_send_key_press(spice, ev->keycode, ev->keysym);
        break;
    }

    return false;
}

static void handle_mouse(spice_t *spice, const tty_event_t *ev)
{
    struct winsize win;

    if (ev->motion || ev->button == 0) {
        /* convert cell coordinates to terminal pixels, the SPICE
         * server expects the position scaled to its own resolution */
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &win) == 0 &&
                win.ws_col > 0 && win.ws_row > 0 &&
                win.ws_xpixel > 0 && win.ws_ypixel > 0) {
            int cell_w = win.ws_xpixel / win.ws_col;
            int cell_h = win.ws_ypixel / win.ws_row;

            spice_send_mouse_motion(spice,
                    (ev->x - 1) * cell_w + cell_w / 2,
                    (ev->y - 1) * cell_h + cell_h / 2);
        }
        return;
    }

    if (ev->button == 4 || ev->button == 5) {
        /* wheel: SGR reports the press only, synthesize the release */
        spice_send_mouse_button_press(spice, ev->button);
        spice_send_mouse_button_release(spice, ev->button);
        return;
    }

    if (ev->release) {
        spice_send_mouse_button_release(spice, ev->button);
    } else {
        spice_send_mouse_button_press(spice, ev->button);
    }
}

int input_tty_init(spice_t *spice, bool mouse_hide)
{
    struct termios raw;

    (void) spice;
    (void) mouse_hide; /* the terminal draws its own cursor */

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "tty input backend requires a terminal on stdin\n");
        return -1;
    }
    if (tcgetattr(STDIN_FILENO, &orig_termios) != 0) {
        fprintf(stderr, "%s: %s\n", __func__, strerror(errno));
        return -1;
    }
    termios_saved = true;

    raw = orig_termios;
    cfmakeraw(&raw);
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        fprintf(stderr, "%s: %s\n", __func__, strerror(errno));
        return -1;
    }

    atexit(tty_restore);
    signal(SIGPIPE, tty_signal_handler);
    signal(SIGINT, tty_signal_handler);
    signal(SIGTERM, tty_signal_handler);
    signal(SIGHUP, tty_signal_handler);
    signal(SIGSEGV, tty_signal_handler);
    signal(SIGABRT, tty_signal_handler);

    fputs(MOUSE_ON KBD_MODE_PUSH, stdout);
    fflush(stdout);

    return 0;
}

int input_tty_run(spice_t *spice)
{
    uint8_t buf[READ_BUF_LEN];
    size_t pending = 0;
    uint64_t pending_ts = 0;
    int rc = 0;

    for (;;) {
        fd_set fds;
        struct timeval tv;
        int fds_ready;

        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        /* with partial input pending, wake up early to flush stuck
         * sequences (e.g. a lone Esc from a terminal without kitty
         * protocol support) */
        tv.tv_sec = 0;
        tv.tv_usec = (pending ? INPUT_FLUSH_MS : 1000) * 1000;

        fds_ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        if (fds_ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            rc = 1;
            break;
        }

        if (spice_is_canceled(spice)) {
            break;
        }

        if (fds_ready > 0) {
            ssize_t r = read(STDIN_FILENO, buf + pending,
                    sizeof(buf) - pending);

            if (r < 0) {
                if (errno == EINTR) {
                    continue;
                }
                rc = 1;
                break;
            }
            if (r == 0) { /* EOF on stdin */
                rc = 1;
                break;
            }
            if (pending == 0) {
                pending_ts = now_ms();
            }
            pending += r;
        } else if (pending == 0) {
            continue;
        }

        size_t off = 0;
        bool quit = false;

        while (off < pending) {
            tty_event_t ev;
            int used = tty_parse(buf + off, pending - off, &ev);

            if (used == 0) {
                if (now_ms() - pending_ts <= INPUT_FLUSH_MS) {
                    break; /* wait for the rest of the sequence */
                }
                used = tty_parse_final(buf + off, pending - off, &ev);
                if (used <= 0) {
                    break;
                }
            }
            if (used < 0) {
                off++; /* skip the broken byte */
                continue;
            }
            off += used;

            if (ev.type == TTY_EV_KEY) {
                quit = handle_key(spice, &ev);
            } else if (ev.type == TTY_EV_MOUSE) {
                handle_mouse(spice, &ev);
            }
            if (quit) {
                break;
            }
        }
        memmove(buf, buf + off, pending - off);
        pending -= off;

        if (quit) {
            break;
        }
    }

    /* restore the terminal before the SPICE threads are joined: they
     * can take up to a second to stop and the terminal would keep
     * queueing mouse reports meanwhile */
    tty_restore();

    return rc;
}

void input_tty_cleanup(void)
{
    tty_restore();
}

const input_backend_t tty_input = {
    .init = input_tty_init,
    .run = input_tty_run,
    .cleanup = input_tty_cleanup,
};
/* vim:set ts=4 sw=4: */
