#ifndef SRUN_H
#define SRUN_H

/* Shared declarations for srun (a tiny rofi/dmenu-like Wayland launcher).
 * Types, the global client state, and function prototypes used across the
 * modules (main.c, apps.c, render.c, input.c). */

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <cairo.h>
#include <xdg-shell-client-protocol.h>

/* ----- layout ----- */
#define W        560
#define VISIBLE  8
#define ROW_H    30
#define HEADER_H 46
#define ICON     22
#define MARGIN   14
#define H        (HEADER_H + VISIBLE * ROW_H)

/* ----- application entry ----- */
typedef struct {
	char *name;
	char *exec;
	char *desktop;
	char *icon;
	cairo_surface_t *icon_surf;   /* loaded lazily by render(), cached here */
} App;

/* ----- global client state ----- */

/* wayland objects (defined in main.c) */
extern struct wl_display      *display;
extern struct wl_compositor   *compositor;
extern struct xdg_wm_base     *wm_base;
extern struct wl_shm          *shm;
extern struct wl_seat         *seat;
extern struct wl_keyboard     *kbd;
extern struct wl_pointer      *pointer;
extern struct wl_surface      *surface;
extern struct xdg_surface     *xdg_surface;
extern struct xdg_toplevel    *xdg_toplevel;
extern struct wl_shm_pool     *pool;
extern struct wl_buffer       *buffer;
extern void                   *shm_data;
extern cairo_surface_t        *csurf;
extern cairo_t                *cr;
extern int                     configured, dirty, quit, kbfocus;

/* xkb (defined in main.c) */
extern struct xkb_context *xkb_ctx;
extern struct xkb_keymap  *xkb_km;
extern struct xkb_state   *xkb_state;

/* application list (defined in apps.c) */
extern App   *apps;
extern int    napps, apps_cap;
extern App  **filtered;
extern int    nfiltered;
extern int    sel, scroll;
extern char   input[256];
extern int    input_len;

/* repeat state shared with the event loop (defined in input.c) */
extern int  repeat_active;
extern long repeat_at;

/* ----- prototypes ----- */

/* apps.c */
void load_config(void);
void load_apps(void);
void rebuild(void);
void run_app(App *a);
void run_in_terminal(const char *cmd);
cairo_surface_t *load_icon(const char *icon);

/* render.c */
void render(void);

/* input.c */
void input_init(void);
void repeat_tick(void);
long now_ms(void);

#endif
