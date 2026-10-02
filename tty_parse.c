/*
 * Parser for terminal keyboard and mouse input: the kitty keyboard
 * protocol (CSI u), legacy functional encodings (CSI ~ / CSI letter /
 * SS3) and SGR mouse reports. Produces linux evdev key codes and
 * X11-compatible keysyms suitable for the libspice input API.
 */
#include <string.h>

#include <libspice.h>

#include "tty_parse.h"

#define TTY_PARAM_MAX 3
#define TTY_PARAM_VAL_MAX 1000000

typedef struct {
    uint32_t code;    /* kitty functional code, ~ number or final letter */
    uint32_t keycode; /* linux evdev code */
    uint32_t keysym;
} tty_keymap_t;

/* linux evdev codes of the letter keys, a-z */
static const uint8_t tty_letters[26] = {
    30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38,
    50, 49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44
};

/* kitty keyboard protocol functional key codes (CSI u) */
static const tty_keymap_t tty_func_keys[] = {
    { 27,    1,   0xff1b }, /* Esc */
    { 13,    28,  0xff0d }, /* Enter */
    { 9,     15,  0xff09 }, /* Tab */
    { 127,   14,  0xff08 }, /* Backspace */
    { 57358, 58,  0xffe5 }, /* CapsLock */
    { 57359, 70,  0xff14 }, /* ScrollLock */
    { 57360, 69,  0xff7f }, /* NumLock */
    { 57361, 99,  0xff15 }, /* PrintScreen */
    { 57362, 119, 0xff13 }, /* Pause */
    { 57363, 127, 0xff20 }, /* Menu */
    { 57399, 82,  0xffb0 }, /* KP 0 */
    { 57400, 79,  0xffb1 }, /* KP 1 */
    { 57401, 80,  0xffb2 }, /* KP 2 */
    { 57402, 81,  0xffb3 }, /* KP 3 */
    { 57403, 75,  0xffb4 }, /* KP 4 */
    { 57404, 76, 0xffb5 }, /* KP 5 */
    { 57405, 77,  0xffb6 }, /* KP 6 */
    { 57406, 71,  0xffb7 }, /* KP 7 */
    { 57407, 72,  0xffb8 }, /* KP 8 */
    { 57408, 73,  0xffb9 }, /* KP 9 */
    { 57409, 83,  0xffae }, /* KP Decimal */
    { 57410, 98,  0xffaf }, /* KP Divide */
    { 57411, 55,  0xffaa }, /* KP Multiply */
    { 57412, 74,  0xffad }, /* KP Subtract */
    { 57413, 78,  0xffab }, /* KP Add */
    { 57414, 96,  0xff8d }, /* KP Enter */
    { 57415, 117, 0xffbd }, /* KP Equal */
    { 57416, 121, 0xffac }, /* KP Separator */
    { 57441, 42,  0xffe1 }, /* Left Shift */
    { 57442, 29,  0xffe3 }, /* Left Control */
    { 57443, 56,  0xffe9 }, /* Left Alt */
    { 57444, 125, 0xffeb }, /* Left Super */
    { 57447, 54,  0xffe2 }, /* Right Shift */
    { 57448, 97,  0xffe4 }, /* Right Control */
    { 57449, 100, 0xffea }, /* Right Alt */
    { 57450, 126, 0xffec }, /* Right Super */
};

/* CSI <number> ~ */
static const tty_keymap_t tty_tilde_keys[] = {
    { 2,  110, 0xff63 }, /* Insert */
    { 3,  111, 0xffff }, /* Delete */
    { 5,  104, 0xff55 }, /* PageUp */
    { 6,  109, 0xff56 }, /* PageDown */
    { 7,  102, 0xff50 }, /* Home */
    { 8,  107, 0xff57 }, /* End */
    { 11, 59,  0xffbe }, /* F1 */
    { 12, 60,  0xffbf }, /* F2 */
    { 13, 61,  0xffc0 }, /* F3 */
    { 14, 62,  0xffc1 }, /* F4 */
    { 15, 63,  0xffc2 }, /* F5 */
    { 17, 64,  0xffc3 }, /* F6 */
    { 18, 65,  0xffc4 }, /* F7 */
    { 19, 66,  0xffc5 }, /* F8 */
    { 20, 67,  0xffc6 }, /* F9 */
    { 21, 68,  0xffc7 }, /* F10 */
    { 23, 87,  0xffc8 }, /* F11 */
    { 24, 88,  0xffc9 }, /* F12 */
};

/* CSI <number> <letter> and SS3 <letter> final bytes */
static const tty_keymap_t tty_letter_keys[] = {
    { 'A', 103, SPICE_KS_UP },    /* Up */
    { 'B', 108, SPICE_KS_DOWN },  /* Down */
    { 'C', 106, SPICE_KS_RIGHT }, /* Right */
    { 'D', 105, SPICE_KS_LEFT },  /* Left */
    { 'H', 102, 0xff50 },         /* Home */
    { 'F', 107, 0xff57 },         /* End */
    { 'P', 59,  0xffbe },         /* F1 */
    { 'Q', 60,  0xffbf },         /* F2 */
    { 'R', 61,  0xffc0 },         /* F3 */
    { 'S', 62,  0xffc1 },         /* F4 */
};

static const tty_keymap_t *tty_keymap_lookup(const tty_keymap_t *map,
        size_t count, uint32_t code)
{
    for (size_t i = 0; i < count; i++) {
        if (map[i].code == code) {
            return &map[i];
        }
    }
    return NULL;
}

static uint32_t tty_ascii_to_evdev(uint32_t cp)
{
    if (cp >= 'a' && cp <= 'z') {
        return tty_letters[cp - 'a'];
    }
    if (cp >= 'A' && cp <= 'Z') {
        return tty_letters[cp - 'A'];
    }

    switch (cp) {
    case ' ': return 57;
    case '1': case '!': return 2;
    case '2': case '@': return 3;
    case '3': case '#': return 4;
    case '4': case '$': return 5;
    case '5': case '%': return 6;
    case '6': case '^': return 7;
    case '7': case '&': return 8;
    case '8': case '*': return 9;
    case '9': case '(': return 10;
    case '0': case ')': return 11;
    case '-': case '_': return 12;
    case '=': case '+': return 13;
    case '[': case '{': return 26;
    case ']': case '}': return 27;
    case '\\': case '|': return 43;
    case ';': case ':': return 39;
    case '\'': case '"': return 40;
    case '`': case '~': return 41;
    case ',': case '<': return 51;
    case '.': case '>': return 52;
    case '/': case '?': return 53;
    default: return 0;
    }
}

/* decode one UTF-8 character, returns its length, 0 if incomplete */
static int tty_utf8_decode(const uint8_t *buf, size_t len, uint32_t *cp)
{
    uint32_t v;
    int n;

    if (buf[0] < 0x80) {
        *cp = buf[0];
        return 1;
    }
    if ((buf[0] & 0xe0) == 0xc0) {
        n = 2;
        v = buf[0] & 0x1f;
    } else if ((buf[0] & 0xf0) == 0xe0) {
        n = 3;
        v = buf[0] & 0x0f;
    } else if ((buf[0] & 0xf8) == 0xf0) {
        n = 4;
        v = buf[0] & 0x07;
    } else {
        return -1;
    }
    if ((size_t) n > len) {
        return 0;
    }
    for (int i = 1; i < n; i++) {
        if ((buf[i] & 0xc0) != 0x80) {
            return -1;
        }
        v = (v << 6) | (buf[i] & 0x3f);
    }
    *cp = v;
    return n;
}

/* US layout characters that are only produced with Shift held */
static bool tty_cp_needs_shift(uint32_t cp)
{
    if (cp >= 'A' && cp <= 'Z') {
        return true;
    }
    return cp > 0 && cp < 127 &&
        strchr("!@#$%^&*()_+{}|:\"<>?~", (char) cp) != NULL;
}

/* plain text (or control byte) key press, optionally with a legacy
 * Alt modifier encoded as an Esc prefix */
static int tty_parse_text(const uint8_t *buf, size_t len, tty_event_t *ev,
        unsigned int mods)
{
    static const uint8_t ctrl_keys[4] = { 43, 27, 7, 12 }; /* \ ] ^ _ */
    uint32_t cp;
    int n;

    if ((n = tty_utf8_decode(buf, len, &cp)) <= 0) {
        return n;
    }

    ev->type = TTY_EV_KEY;
    ev->mods = mods;
    ev->event = TTY_EV_TAP;

    switch (cp) {
    case 13:
        ev->keycode = 28;
        ev->keysym = 0xff0d;
        return n;
    case 9:
        ev->keycode = 15;
        ev->keysym = 0xff09;
        return n;
    case 127:
        ev->keycode = 14;
        ev->keysym = 0xff08;
        return n;
    default:
        break;
    }

    if (cp >= 1 && cp <= 26) {
        ev->keycode = tty_letters[cp - 1];
        ev->keysym = cp + 96;
        ev->mods |= TTY_MOD_CTRL;
        return n;
    }
    if (cp >= 28 && cp <= 31) {
        ev->keycode = ctrl_keys[cp - 28];
        ev->keysym = cp + 64;
        ev->mods |= TTY_MOD_CTRL;
        return n;
    }

    ev->keycode = tty_ascii_to_evdev(cp);
    ev->keysym = cp;
    if (ev->keycode && tty_cp_needs_shift(cp)) {
        /* an upper-case character could only arrive with Shift held,
         * the terminal does not report the modifier itself */
        ev->mods |= TTY_MOD_SHIFT;
    }
    return n;
}

/* fill a key event from a codepoint: primary, then the kitty alternate
 * key sub-parameters (shifted key, base layout key) as a fallback for
 * non-latin keyboard layouts */
static bool tty_key_from_codepoint(uint32_t cp, uint32_t alt,
        tty_event_t *ev)
{
    if (cp && tty_ascii_to_evdev(cp)) {
        ev->keycode = tty_ascii_to_evdev(cp);
        ev->keysym = cp;
        return true;
    }
    if (alt && tty_ascii_to_evdev(alt)) {
        ev->keycode = tty_ascii_to_evdev(alt);
        ev->keysym = alt;
        return true;
    }
    if (cp > 127) {
        /* non-latin glyph without a latin alternate: unmappable */
        ev->keycode = 0;
        ev->keysym = cp;
        return true;
    }
    return false;
}

static int tty_parse_ss3(const uint8_t *buf, tty_event_t *ev)
{
    const tty_keymap_t *k = tty_keymap_lookup(tty_letter_keys,
            sizeof(tty_letter_keys) / sizeof(tty_letter_keys[0]), buf[2]);

    ev->type = k ? TTY_EV_KEY : TTY_EV_IGNORE;
    ev->event = TTY_EV_TAP;
    if (k) {
        ev->keycode = k->keycode;
        ev->keysym = k->keysym;
    }
    return 3;
}

static int tty_parse_csi(const uint8_t *buf, size_t len, tty_event_t *ev)
{
    uint32_t v[TTY_PARAM_MAX] = {0};
    uint32_t sub1[TTY_PARAM_MAX] = {0}; /* first ':' sub-parameter */
    uint32_t sub2[TTY_PARAM_MAX] = {0}; /* second ':' sub-parameter */
    bool has_params, in_sub = false;
    int sub_index, nfields = 0;
    size_t i = 2, end;
    char priv = 0;
    uint8_t final;

    if (i < len && (buf[i] == '<' || buf[i] == '?' || buf[i] == '>' ||
            buf[i] == '=')) {
        priv = (char) buf[i];
        i++;
    }

    for (end = i; end < len; end++) {
        if (buf[end] >= 0x40 && buf[end] <= 0x7e) {
            break; /* final byte found */
        }
        if (buf[end] < 0x20 || buf[end] > 0x3f) {
            return -1; /* unexpected intermediate byte */
        }
    }
    if (end == len) {
        return 0; /* wait for the final byte */
    }

    has_params = end > i;
    sub_index = 0;
    for (size_t j = i; j < end; j++) {
        uint8_t c = buf[j];

        if (c >= '0' && c <= '9') {
            if (nfields < TTY_PARAM_MAX) {
                uint32_t *p = !in_sub ? &v[nfields] :
                    sub_index == 1 ? &sub1[nfields] : &sub2[nfields];
                if (*p < TTY_PARAM_VAL_MAX) {
                    *p = *p * 10 + (c - '0');
                }
            }
        } else if (c == ':') {
            if (++sub_index > 2) {
                sub_index = 2; /* deeper sub-parameters are ignored */
            }
            in_sub = true;
        } else if (c == ';') {
            nfields++;
            in_sub = false;
            sub_index = 0;
            if (nfields >= TTY_PARAM_MAX && j + 1 < end) {
                ev->type = TTY_EV_IGNORE;
                return end + 1;
            }
        }
        /* 0x20-0x2f intermediates are skipped */
    }

    final = buf[end];

    /* SGR mouse report: CSI < button ; x ; y M/m */
    if (priv == '<' && (final == 'M' || final == 'm')) {
        uint32_t cb;

        if (nfields < 2) {
            ev->type = TTY_EV_IGNORE;
            return end + 1;
        }
        cb = v[0];
        ev->type = TTY_EV_MOUSE;
        ev->x = v[1];
        ev->y = v[2];
        ev->release = final == 'm';
        if (cb & 64) {
            ev->button = (cb & 3) == 0 ? 4 : 5; /* wheel up/down */
        } else if (cb & 128) {
            ev->button = (cb & 3) == 0 ? 6 : 7; /* side buttons */
        } else if ((cb & 3) == 3) {
            ev->motion = true;
        } else {
            ev->button = (cb & 3) + 1;
            ev->motion = cb & 32;
        }
        return end + 1;
    }

    /* other private sequences (mode reports etc.) are not input */
    if (priv) {
        ev->type = TTY_EV_IGNORE;
        return end + 1;
    }

    /* modifiers + event type from the second field, e.g. ;5:3 */
    if (nfields >= 1) {
        ev->mods = v[1] ? v[1] - 1 : 0;
        ev->event = sub1[1] ? sub1[1] : TTY_EV_TAP;
    } else {
        ev->mods = 0;
        ev->event = TTY_EV_TAP;
    }

    if (final == 'u') {
        const tty_keymap_t *k;
        uint32_t code = v[0];

        if (!has_params || code == 0) {
            ev->type = TTY_EV_IGNORE;
            return end + 1;
        }

        k = tty_keymap_lookup(tty_func_keys,
                sizeof(tty_func_keys) / sizeof(tty_func_keys[0]), code);
        if (k) {
            ev->type = TTY_EV_KEY;
            ev->keycode = k->keycode;
            ev->keysym = k->keysym;
            return end + 1;
        }
        if (code >= 57376 && code <= 57387) { /* F13-F24 */
            ev->type = TTY_EV_KEY;
            ev->keycode = 183 + (code - 57376);
            ev->keysym = 0xffda + (code - 57376);
            return end + 1;
        }
        ev->type = TTY_EV_KEY;
        if (tty_key_from_codepoint(code, sub2[0] ? sub2[0] : sub1[0], ev)) {
            return end + 1;
        }
        ev->type = TTY_EV_IGNORE;
        return end + 1;
    }

    if (final == '~') {
        const tty_keymap_t *k = tty_keymap_lookup(tty_tilde_keys,
                sizeof(tty_tilde_keys) / sizeof(tty_tilde_keys[0]), v[0]);

        ev->type = k ? TTY_EV_KEY : TTY_EV_IGNORE;
        if (k) {
            ev->keycode = k->keycode;
            ev->keysym = k->keysym;
        }
        return end + 1;
    }

    if (final >= 'A' && final <= 'Z') {
        const tty_keymap_t *k = tty_keymap_lookup(tty_letter_keys,
                sizeof(tty_letter_keys) / sizeof(tty_letter_keys[0]), final);

        /* CSI row ; col R is a cursor position report, not an F3 press */
        if (k && !(final == 'R' && nfields >= 1 && v[0] > 1)) {
            ev->type = TTY_EV_KEY;
            ev->keycode = k->keycode;
            ev->keysym = k->keysym;
        } else {
            ev->type = TTY_EV_IGNORE;
        }
        return end + 1;
    }

    ev->type = TTY_EV_IGNORE;
    return end + 1;
}

int tty_parse(const uint8_t *buf, size_t len, tty_event_t *ev)
{
    int n;

    memset(ev, 0, sizeof(*ev));
    if (len == 0) {
        return 0;
    }
    if (buf[0] != 0x1b) {
        return tty_parse_text(buf, len, ev, 0);
    }
    if (len == 1) {
        return 0;
    }
    if (buf[1] == '[') {
        return tty_parse_csi(buf, len, ev);
    }
    if (buf[1] == 'O') {
        if (len < 3) {
            return 0;
        }
        return tty_parse_ss3(buf, ev);
    }

    /* legacy Alt modifier: Esc followed by a text key */
    n = tty_parse_text(buf + 1, len - 1, ev, TTY_MOD_ALT);
    if (n <= 0) {
        return n;
    }
    return n + 1;
}

int tty_parse_final(const uint8_t *buf, size_t len, tty_event_t *ev)
{
    int n = tty_parse(buf, len, ev);

    if (n != 0) {
        return n;
    }

    /* a lone Esc that nothing followed: a real Esc key press */
    memset(ev, 0, sizeof(*ev));
    if (buf[0] == 0x1b && len == 1) {
        ev->type = TTY_EV_KEY;
        ev->keycode = 1;
        ev->keysym = 0xff1b;
        ev->event = TTY_EV_TAP;
        return 1;
    }

    /* truncated garbage */
    ev->type = TTY_EV_IGNORE;
    return len;
}
/* vim:set ts=4 sw=4: */
