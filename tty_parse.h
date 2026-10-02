#ifndef TTY_PARSE_H_
#define TTY_PARSE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Event types: a terminal without kitty event-type support reports
 * key presses only, so sequences without an event-type sub-parameter
 * are reported as a tap and the caller sends press + release. */
#define TTY_EV_KEY    0
#define TTY_EV_MOUSE  1
#define TTY_EV_IGNORE 2 /* sequence consumed, no event */

#define TTY_EV_TAP     0
#define TTY_EV_PRESS   1
#define TTY_EV_REPEAT  2
#define TTY_EV_RELEASE 3

#define TTY_MOD_SHIFT 0x01
#define TTY_MOD_ALT   0x02
#define TTY_MOD_CTRL  0x04
#define TTY_MOD_SUPER 0x08

typedef struct {
    int type;
    /* keyboard */
    uint32_t keycode; /* linux evdev code, 0 if unmappable */
    uint32_t keysym;  /* X11-compatible keysym value */
    unsigned int mods;
    int event;        /* TTY_EV_* */
    /* mouse (SGR, cell coordinates, 1-based) */
    unsigned int button; /* 0 = none, 1-3 buttons, 4/5 wheel, 6/7 side */
    int x, y;
    bool motion;
    bool release;
} tty_event_t;

/* Parse one event from buf. Returns the number of bytes consumed:
 *   >0  event stored in *ev (may be TTY_EV_IGNORE)
 *    0  incomplete sequence, call again with more data
 *   -1  invalid sequence, skip one byte and retry */
int tty_parse(const uint8_t *buf, size_t len, tty_event_t *ev);

/* Same, but instead of waiting for more data settles incomplete input
 * that is known to be final (e.g. a lone Esc key press). */
int tty_parse_final(const uint8_t *buf, size_t len, tty_event_t *ev);

#endif /* TTY_PARSE_H_ */
/* vim:set ts=4 sw=4: */
