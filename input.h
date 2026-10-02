#ifndef INPUT_H_
#define INPUT_H_

#include <stdbool.h>

#include <libspice.h>

typedef struct {
    int (*init)(spice_t *spice, bool mouse_hide);
    int (*run)(spice_t *spice);
    void (*cleanup)(void); /* called after the SPICE threads are joined */
} input_backend_t;

extern const input_backend_t tty_input;

#ifdef HAVE_X11
extern const input_backend_t x11_input;
#endif

#endif /* INPUT_H_ */
/* vim:set ts=4 sw=4: */
