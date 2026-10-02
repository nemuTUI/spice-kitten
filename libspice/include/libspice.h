#ifndef LIBSPICE_H_
#define LIBSPICE_H_

#define SPICE_DEFAULT_PORT 5900
#define SPICE_DEFAULT_ADDR "127.0.0.1"

/* keysym values (X11-compatible) recognized by the input API */
#define SPICE_KS_LEFT  0xff51u
#define SPICE_KS_UP    0xff52u
#define SPICE_KS_RIGHT 0xff53u
#define SPICE_KS_DOWN  0xff54u

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _cplusplus
extern "C" {
#endif

typedef struct spice spice_t;

spice_t *spice_init(const char *addr, uint16_t port, const char *log);
void spice_deinit(spice_t *spice);
void spice_cancel(spice_t *spice);
bool spice_is_canceled(const spice_t *spice);
bool spice_channel_init_main(spice_t *spice);
bool spice_channel_init_inputs(spice_t *spice);
void *spice_channel_main_loop(void *ctx);
void *spice_channel_display_loop(void *ctx); /* channel init is not required */
void *spice_draw_screen(void *ctx);
/* keycode is a linux evdev code, keysym is an X11-compatible keysym value
 * (only SPICE_KS_LEFT/UP/RIGHT/DOWN are translated to AT scancodes) */
void spice_send_key_release(spice_t *spice, uint32_t keycode, uint32_t keysym);
void spice_send_key_press(spice_t *spice, uint32_t keycode, uint32_t keysym);
void spice_send_mouse_motion(spice_t *spice, int x, int y);
void spice_send_mouse_button_release(spice_t *spice, unsigned int button);
void spice_send_mouse_button_press(spice_t *spice, unsigned int button);

#ifdef _cplusplus
}
#endif

#endif /* LIBSPICE_H_ */
/* vim:set ts=4 sw=4: */
