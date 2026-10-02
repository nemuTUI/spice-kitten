#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>

#include <libspice.h>

#include "input.h"

#define LOG_PATH "/tmp/spice_client.log"

int main(int argc, char **argv)
{
    const input_backend_t *backend = NULL;
    pthread_t display_th, main_th, screen_th;
    char *spice_addr = NULL;
    char *log_path = NULL;
    const char *backend_name = "tty";
    int port = SPICE_DEFAULT_PORT;
    bool mouse_hide = false;
    bool system_clear = true;
    spice_t *spice;
    int rc = 0, opt;

#ifdef HAVE_X11
    while ((opt = getopt(argc, argv, "a:p:b:vmhc")) != -1) {
#else
    while ((opt = getopt(argc, argv, "a:p:vmhc")) != -1) {
#endif
        switch (opt) {
        case 'a':
            spice_addr = strdup(optarg);
            if (!spice_addr) {
                fprintf(stderr, "%s: %s\n", __func__, strerror(errno));
                exit(EXIT_FAILURE);
            }
            break;
        case 'p':
            if ((port = atoi(optarg)) == 0) {
                fprintf(stderr, "bad port value\n");
                exit(EXIT_FAILURE);
            }
            break;
#ifdef HAVE_X11
        case 'b':
            if (strcmp(optarg, "tty") != 0 && strcmp(optarg, "x11") != 0) {
                fprintf(stderr, "unknown backend: %s\n", optarg);
                exit(EXIT_FAILURE);
            }
            backend_name = optarg;
            break;
#endif
        case 'v':
            log_path = strdup(LOG_PATH);
            if (!log_path) {
                fprintf(stderr, "%s: %s\n", __func__, strerror(errno));
                exit(EXIT_FAILURE);
            }
            break;
        case 'm':
            mouse_hide = true;
            break;
        case 'c':
            system_clear = false;
            break;
        case 'h':
            printf("Usage: %s\n"
                   "Options:\n"
                   " -a <addr>    - IPv4 address (default: %s)\n"
                   " -p <port>    - SPICE port (default: %u)\n"
#ifdef HAVE_X11
                   " -b <backend> - input backend: tty(default), x11\n"
#endif
                   " -v           - enable log\n"
                   " -m           - hide mouse cursor\n"
                   " -c           - clear screen using an escape sequence\n"
                   " -h           - print help and exit\n",
                   *argv, SPICE_DEFAULT_ADDR, SPICE_DEFAULT_PORT);
            exit(EXIT_SUCCESS);
        }
    }

    if (!spice_addr) {
        spice_addr = strdup(SPICE_DEFAULT_ADDR);
        if (!spice_addr) {
            fprintf(stderr, "%s: %s\n", __func__, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    if (strcmp(backend_name, "tty") == 0) {
        backend = &tty_input;
#ifdef HAVE_X11
    } else if (strcmp(backend_name, "x11") == 0) {
        backend = &x11_input;
#endif
    }

    if ((spice = spice_init(spice_addr, port, log_path)) == NULL) {
        fprintf(stderr, "spice_init failed\n");
        exit(EXIT_FAILURE);
    }

    if (!spice_channel_init_main(spice)) {
        fprintf(stderr, "init main channel failed\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&main_th, NULL,
                spice_channel_main_loop, spice) != 0) {
        fprintf(stderr, "failed to create session control channel thread\n");
        exit(EXIT_FAILURE);
    }

    if (!spice_channel_init_inputs(spice)) {
        fprintf(stderr, "init inputs channel failed\n");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&display_th, NULL,
                spice_channel_display_loop, spice) != 0) {
        fprintf(stderr, "failed to create display channel thread\n");
        exit(EXIT_FAILURE);
    }

    if (system_clear) {
        system("clear");
    } else {
        printf("\033[0;0H");
    }

    if (backend->init(spice, mouse_hide) != 0) {
        rc = 1;
        goto out;
    }

    if (pthread_create(&screen_th, NULL,
                spice_draw_screen, spice) != 0) {
        fprintf(stderr, "failed to create screen thread\n");
        backend->cleanup();
        exit(EXIT_FAILURE);
    }

    rc = backend->run(spice);

    if (rc == 0) {
        pthread_join(screen_th, NULL);
        pthread_join(display_th, NULL);
        pthread_join(main_th, NULL);
        spice_deinit(spice);
    }
    backend->cleanup();

out:
    free(log_path);
    free(spice_addr);
    return rc;
}
/* vim:set ts=4 sw=4: */
