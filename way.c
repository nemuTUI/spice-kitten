#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

int main(void)
{
    struct wl_display *display;

    if (((display = wl_display_connect(NULL))) == NULL) {
        fprintf(stderr, "failed to conenct to Wayland display\n");
        return 1;
    }

    printf("OK\n");

    return 0;
}
