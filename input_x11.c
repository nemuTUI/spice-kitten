/* X11 input backend: grabs the keyboard and the pointer of the focused
 * window and forwards the events to the SPICE server. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/select.h>

#include <X11/X.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/cursorfont.h>

#include <libspice.h>

#include "input.h"

static Display *display;
static Cursor xcursor;

int input_x11_init(spice_t *spice, bool mouse_hide)
{
    Window root_window, focus_window;
    int revert_to;

    (void) spice;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "unable to open X display\n");
        return -1;
    }

    root_window = DefaultRootWindow(display);
    xcursor = XCreateFontCursor(display, XC_arrow);
    XGetInputFocus(display, &focus_window, &revert_to);
    XGrabKeyboard(display, root_window, False,
            GrabModeAsync, GrabModeAsync, CurrentTime);
    XGrabPointer(display, focus_window, False,
            PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
            GrabModeAsync, GrabModeAsync, focus_window,
            mouse_hide ? None : xcursor, CurrentTime);

    return 0;
}

int input_x11_run(spice_t *spice)
{
    bool ctrl_down = false;
    int x11_fd = ConnectionNumber(display);
    int rc = 0;

    for (;;) {
        fd_set fdset;
        struct timeval tv;
        int fds;

        FD_ZERO(&fdset);
        FD_SET(x11_fd, &fdset);
        tv.tv_usec = 0;
        tv.tv_sec = 1;

        fds = select(x11_fd + 1, &fdset, NULL, NULL, &tv);
        if (fds < 0) {
            rc = 1;
            break;
        }

        if (spice_is_canceled(spice)) {
            break;
        }

        while (XPending(display)) {
            XEvent event;
            KeySym keysym;

            XNextEvent(display, &event);
            /* an X keycode is an evdev code + 8 */
            keysym = XkbKeycodeToKeysym(display, event.xkey.keycode, 0, 0);

            switch (event.type) {
            case KeyPress:
                if (keysym == XK_Control_L) {
                    ctrl_down = true;
                } else if (keysym != XK_q && ctrl_down) {
                    ctrl_down = false;
                }

                if (ctrl_down && keysym == XK_q) {
                    spice_cancel(spice);
                    return 0;
                }

                spice_send_key_press(spice,
                        event.xkey.keycode - 8, keysym);
                break;
            case KeyRelease:
                spice_send_key_release(spice,
                        event.xkey.keycode - 8, keysym);
                break;
            case MotionNotify: {
                XButtonEvent *mouse_event;

                mouse_event = (XButtonEvent *) &event;
                spice_send_mouse_motion(spice,
                        mouse_event->x, mouse_event->y);
                }
                break;
            case ButtonPress:
                spice_send_mouse_button_press(spice, event.xbutton.button);
                break;
            case ButtonRelease:
                spice_send_mouse_button_release(spice, event.xbutton.button);
                break;
            }
        }
    }

    return rc;
}

void input_x11_cleanup(void)
{
    XUngrabKeyboard(display, CurrentTime);
    XUngrabPointer(display, CurrentTime);
    XFreeCursor(display, xcursor);
    XCloseDisplay(display);
}

const input_backend_t x11_input = {
    .init = input_x11_init,
    .run = input_x11_run,
    .cleanup = input_x11_cleanup,
};
/* vim:set ts=4 sw=4: */
