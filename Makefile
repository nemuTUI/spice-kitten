all:
	gcc way.c -o way `pkg-config --cflags --libs wayland-client` -lxkbcommon
