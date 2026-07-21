// srun - a tiny rofi/dmenu-like application launcher for swm (or any
// xdg-shell Wayland compositor).
//
// Modules:
//   srun.h    shared types and prototypes
//   main.c    wayland/shell setup, shared client state, event loop
//   apps.c    application list, .desktop parsing, config, launching, icons
//   render.c  cairo rendering of the launcher window
//   input.c   keyboard and pointer handling, auto-repeat

#include "srun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <poll.h>

/* ----- global client state (shared via srun.h) ----- */

struct wl_display      *display;
struct wl_compositor   *compositor;
struct xdg_wm_base     *wm_base;
struct wl_shm          *shm;
struct wl_seat         *seat;
struct wl_keyboard     *kbd;
struct wl_pointer      *pointer;
struct wl_surface      *surface;
struct xdg_surface     *xdg_surface;
struct xdg_toplevel    *xdg_toplevel;
struct wl_shm_pool     *pool;
struct wl_buffer       *buffer;
void                   *shm_data;
cairo_surface_t        *csurf;
cairo_t               *cr;
int                     configured, dirty, quit, kbfocus;

struct xkb_context *xkb_ctx;
struct xkb_keymap  *xkb_km;
struct xkb_state   *xkb_state;

/* ----- registry ----- */

static void registry_global(void *data, struct wl_registry *reg,
		uint32_t id, const char *iface, uint32_t ver) {
	(void)data;
	if (!strcmp(iface, wl_compositor_interface.name))
		compositor = wl_registry_bind(reg, id, &wl_compositor_interface, ver);
	else if (!strcmp(iface, xdg_wm_base_interface.name))
		wm_base = wl_registry_bind(reg, id, &xdg_wm_base_interface, ver);
	else if (!strcmp(iface, wl_shm_interface.name))
		shm = wl_registry_bind(reg, id, &wl_shm_interface, ver);
	else if (!strcmp(iface, wl_seat_interface.name))
		seat = wl_registry_bind(reg, id, &wl_seat_interface, ver);
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t id) {
	(void)data; (void)reg; (void)id;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global, .global_remove = registry_global_remove
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *b, uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(b, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = xdg_wm_base_ping
};

static void xdg_surface_configure(void *data, struct xdg_surface *s, uint32_t serial) {
	(void)data;
	xdg_surface_ack_configure(s, serial);
	configured = 1;
	dirty = 1;
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_configure
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *t,
		int32_t w, int32_t h, struct wl_array *states) {
	(void)data; (void)t; (void)w; (void)h; (void)states;
}

static void xdg_toplevel_configure_bounds(void *data, struct xdg_toplevel *t,
		int32_t width, int32_t height) {
	(void)data; (void)t; (void)width; (void)height;
}

static void xdg_toplevel_wm_capabilities(void *data, struct xdg_toplevel *t,
		struct wl_array *caps) {
	(void)data; (void)t; (void)caps;
}

static void xdg_toplevel_close(void *data, struct xdg_toplevel *t) {
	(void)data; (void)t;
	quit = 1;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_configure, .close = xdg_toplevel_close,
	.configure_bounds = xdg_toplevel_configure_bounds,
	.wm_capabilities = xdg_toplevel_wm_capabilities
};

/* ----- shared memory buffer ----- */

static void shm_init(void) {
	int stride = W * 4;
	int size = stride * H;
	int fd = memfd_create("srun", MFD_CLOEXEC);
	if (fd < 0) {
		char nm[32];
		snprintf(nm, sizeof nm, "/srun-%d", (int)getpid());
		fd = shm_open(nm, O_CREAT | O_RDWR | O_EXCL, 0600);
	}
	if (fd < 0) { perror("srun: shm"); exit(1); }
	ftruncate(fd, size);
	shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	pool = wl_shm_create_pool(shm, fd, size);
	buffer = wl_shm_pool_create_buffer(pool, 0, W, H, stride, WL_SHM_FORMAT_ARGB8888);
	csurf = cairo_image_surface_create_for_data(shm_data, CAIRO_FORMAT_ARGB32, W, H, stride);
	cr = cairo_create(csurf);
	close(fd);
}

static void surface_init(void) {
	surface = wl_compositor_create_surface(compositor);
	xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
	xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
	xdg_toplevel_set_title(xdg_toplevel, "srun");
	xdg_toplevel_set_app_id(xdg_toplevel, "srun");

	input_init();

	wl_surface_commit(surface);
}

int main(void) {
	display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "srun: cannot connect to a Wayland display\n");
		return 1;
	}
	xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

	struct wl_registry *reg = wl_display_get_registry(display);
	wl_registry_add_listener(reg, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (!compositor || !wm_base || !shm) {
		fprintf(stderr, "srun: missing required Wayland interfaces\n");
		return 1;
	}

	shm_init();
	surface_init();
	load_config();
	load_apps();
	rebuild();
	load_theme();

	wl_display_roundtrip(display); /* receive initial configure + keymap */
	dirty = 1;

	while (!quit) {
		if (configured && dirty) { render(); dirty = 0; }

		int timeout = -1;
		if (repeat_active) {
			long rem = repeat_at - now_ms();
			timeout = rem > 0 ? (int)rem : 0;
		}

		int flushed = wl_display_flush(display);
		short ev = POLLIN;
		if (flushed != 0) ev |= POLLOUT;   /* wait for writable only if blocked */

		struct pollfd pfd = { wl_display_get_fd(display), ev, 0 };
		int n = poll(&pfd, 1, timeout);
		if (n > 0) {
			if (pfd.revents & POLLOUT) wl_display_flush(display);
			if (pfd.revents & POLLIN)
				if (wl_display_dispatch(display) < 0) break;
		} else if (n == 0) {
			repeat_tick();
		}
	}
	return 0;
}
