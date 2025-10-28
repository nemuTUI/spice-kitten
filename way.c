#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
//#include <linux/input.h>

struct seat_data {
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    int pointer_grabbed;
    int keyboard_grabbed;
};

static struct wl_display *display = NULL;
static struct wl_compositor *compositor = NULL;
static struct wl_surface *surface = NULL;
static struct seat_data seat = {0};

// Pointer listeners
static void pointer_enter(void *data, struct wl_pointer *pointer,
                         uint32_t serial, struct wl_surface *surface,
                         wl_fixed_t x, wl_fixed_t y) {
    printf("Pointer entered surface\n");
}

static void pointer_leave(void *data, struct wl_pointer *pointer,
                         uint32_t serial, struct wl_surface *surface) {
    printf("Pointer left surface\n");
}

static void pointer_motion(void *data, struct wl_pointer *pointer,
                          uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    printf("Pointer motion: %f, %f\n",
           wl_fixed_to_double(x), wl_fixed_to_double(y));
}

static void pointer_button(void *data, struct wl_pointer *pointer,
                          uint32_t serial, uint32_t time,
                          uint32_t button, uint32_t state) {
    printf("Pointer button: %d, state: %s\n",
           button, state == WL_POINTER_BUTTON_STATE_PRESSED ? "pressed" : "released");

    /*if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        // Grab pointer on left click
        if (!seat.pointer_grabbed) {
            printf("Grabbing pointer...\n");
            seat.pointer_grabbed = 1;
        } else {
            printf("Ungrabbing pointer...\n");
            seat.pointer_grabbed = 0;
        }
    }*/
}

static void pointer_axis(void *data, struct wl_pointer *pointer,
                        uint32_t time, uint32_t axis, wl_fixed_t value) {
    printf("Pointer axis: %d, value: %f\n", axis, wl_fixed_to_double(value));
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

// Keyboard listeners
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard,
                           uint32_t format, int fd, uint32_t size) {
    printf("Keyboard keymap received\n");
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, struct wl_surface *surface,
                          struct wl_array *keys) {
    printf("Keyboard entered surface\n");
    seat.keyboard_grabbed = 1;
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard,
                          uint32_t serial, struct wl_surface *surface) {
    printf("Keyboard left surface\n");
    seat.keyboard_grabbed = 0;
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard,
                        uint32_t serial, uint32_t time,
                        uint32_t key, uint32_t state) {
    printf("Key: %d, state: %s\n",
           key, state == WL_KEYBOARD_KEY_STATE_PRESSED ? "pressed" : "released");

    // Grab/ungrab keyboard with F1 key
    if (key == 59 && state == WL_KEYBOARD_KEY_STATE_PRESSED) { // F1 key
        if (!seat.keyboard_grabbed) {
            printf("Grabbing keyboard...\n");
            seat.keyboard_grabbed = 1;
        } else {
            printf("Ungrabbing keyboard...\n");
            seat.keyboard_grabbed = 0;
        }
    }

    // Exit on Escape key
    if (key == 1 && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        printf("Escape pressed, exiting...\n");
        exit(0);
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
                              uint32_t serial, uint32_t mods_depressed,
                              uint32_t mods_latched, uint32_t mods_locked,
                              uint32_t group) {
    printf("Keyboard modifiers changed\n");
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap,
    .enter = keyboard_enter,
    .leave = keyboard_leave,
    .key = keyboard_key,
    .modifiers = keyboard_modifiers,
};

// Seat listeners
static void seat_capabilities(void *data, struct wl_seat *seat,
                             uint32_t capabilities) {
    struct seat_data *seat_data = data;
    
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        if (!seat_data->pointer) {
            seat_data->pointer = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(seat_data->pointer, &pointer_listener, seat_data);
            printf("Pointer capability acquired\n");
        }
    } else {
        if (seat_data->pointer) {
            wl_pointer_release(seat_data->pointer);
            seat_data->pointer = NULL;
            seat_data->pointer_grabbed = 0;
        }
    }
    
    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        if (!seat_data->keyboard) {
            seat_data->keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(seat_data->keyboard, &keyboard_listener, seat_data);
            printf("Keyboard capability acquired\n");
        }
    } else {
        if (seat_data->keyboard) {
            wl_keyboard_release(seat_data->keyboard);
            seat_data->keyboard = NULL;
            seat_data->keyboard_grabbed = 0;
        }
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    printf("Seat name: %s\n", name);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

// Registry listeners
static void registry_global(void *data, struct wl_registry *registry,
                           uint32_t name, const char *interface, uint32_t version) {
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        seat.seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(seat.seat, &seat_listener, &seat);
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry,
                                  uint32_t name) {
    printf("Global removed: %d\n", name);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};


int main(void)
{
    struct wl_display *display;

    if (((display = wl_display_connect(NULL))) == NULL) {
        fprintf(stderr, "failed to conenct to Wayland display\n");
        return 1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    
    wl_display_roundtrip(display);
    
    if (!compositor || !seat.seat) {
        fprintf(stderr, "Failed to get required Wayland interfaces\n");
        return 1;
    }

    surface = wl_compositor_create_surface(compositor);
    if (!surface) {
        fprintf(stderr, "Failed to create surface\n");
        return 1;
    }

    printf("Wayland input grab example started:\n");
    printf("- Click left mouse button to grab/ungrab pointer\n");
    printf("- Press F1 to grab/ungrab keyboard\n");
    printf("- Press Escape to exit\n");

    while (wl_display_dispatch(display) != -1) {
        // Main loop - events are handled in callbacks
    }

    // Cleanup
    if (seat.keyboard) wl_keyboard_release(seat.keyboard);
    if (seat.pointer) wl_pointer_release(seat.pointer);
    if (seat.seat) wl_seat_release(seat.seat);
    if (surface) wl_surface_destroy(surface);
    if (compositor) wl_compositor_destroy(compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);

    return 0;
}
