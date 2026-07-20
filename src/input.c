// input.c - keyboard and pointer handling, and client-side auto-repeat.

#include "srun.h"

#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>

/* ----- selection / pointer state ----- */

static int ptr_x = 0, ptr_y = 0, hover = -1, entered = 0;

/* wheel state: discrete notches (mice) take priority; continuous input
 * (trackpads) is accumulated and converted to notches, carrying the
 * fractional remainder across frames */
#define SCROLL_UNIT 10.0   /* continuous wl_fixed units per wheel notch */
static int    wheel_disc = 0;
static double wheel_cont = 0;
static double wheel_carry = 0;

/* ----- key repeat (client-side, driven by the event loop) ----- */

static uint32_t      repeat_key = 0;   /* Wayland keycode being repeated (0 = none) */
static xkb_keycode_t repeat_kc = 0;
static xkb_keysym_t  repeat_sym = 0;
int                  repeat_active = 0;
long                 repeat_at = 0;    /* monotonic ms of next repeat tick */
static int           repeat_rate = 25; /* repeats per second */
static int           repeat_delay = 600; /* ms before first repeat */

long now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void utf8_backspace(void) {
	if (input_len == 0) return;
	while (input_len > 0 && (input[input_len - 1] & 0xC0) == 0x80) input_len--;
	if (input_len > 0) input_len--;
	input[input_len] = 0;
}

/* Side effect of a keypress; called for the initial press and each repeat. */
static void key_action(xkb_keysym_t sym, xkb_keycode_t kc) {
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (input_len > 0 && input[0] == '#') {
			if (nfiltered > 0)
				run_in_terminal(filtered[sel]->name);  /* run selected binary */
			else if (input_len > 1)
				run_in_terminal(input + 1);            /* typed command/args */
		} else if (nfiltered > 0) {
			run_app(filtered[sel]);
		}
		return;
	}
	if (sym == XKB_KEY_Escape) {
		if (input_len > 0 && input[0] == '#') {   /* back out of bin mode */
			input[0] = 0; input_len = 0; rebuild();
			return;
		}
		quit = 1; return;
	}
	if (sym == XKB_KEY_BackSpace || sym == XKB_KEY_Delete) {
		utf8_backspace(); rebuild(); dirty = 1; return;
	}
	if (sym == XKB_KEY_Up)            { if (nfiltered) { sel = sel > 0 ? sel - 1 : 0; dirty = 1; } return; }
	if (sym == XKB_KEY_Down)          { if (nfiltered) { sel = sel < nfiltered - 1 ? sel + 1 : nfiltered - 1; dirty = 1; } return; }
	if (sym == XKB_KEY_Tab)           { if (nfiltered) { sel = (sel + 1) % nfiltered; dirty = 1; } return; }
	if (sym == XKB_KEY_ISO_Left_Tab)  { if (nfiltered) { sel = (sel + nfiltered - 1) % nfiltered; dirty = 1; } return; }
	char buf[8];
	int nn = xkb_state_key_get_utf8(xkb_state, kc, buf, sizeof buf);
	if (nn > 0 && input_len + nn < (int)sizeof(input)) {
		memcpy(input + input_len, buf, nn);
		input_len += nn;
		input[input_len] = 0;
		rebuild();
		dirty = 1;
	}
}

void repeat_tick(void) {
	if (!repeat_active) return;
	key_action(repeat_sym, repeat_kc);
	repeat_at = now_ms() + 1000 / repeat_rate;
}

/* ----- seat / keyboard ----- */

static void seat_capabilities(void *data, struct wl_seat *s, uint32_t caps) {
	(void)data; (void)s; (void)caps;
}
static void seat_name(void *data, struct wl_seat *s, const char *name) {
	(void)data; (void)s; (void)name;
}
static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_capabilities, .name = seat_name
};

static void kb_keymap(void *data, struct wl_keyboard *kb, uint32_t format,
		int32_t fd, uint32_t size) {
	(void)data; (void)kb; (void)format;
	if (xkb_state) xkb_state_unref(xkb_state);
	if (xkb_km)    xkb_keymap_unref(xkb_km);
	void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	xkb_km = xkb_keymap_new_from_buffer(xkb_ctx, map, size - 1,
		XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	if (!xkb_km)
		fprintf(stderr, "srun: failed to parse keyboard keymap\n");
	xkb_state = xkb_state_new(xkb_km);
	munmap(map, size);
	close(fd);
}
static void kb_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
		struct wl_surface *surface, struct wl_array *keys) {
	(void)data; (void)kb; (void)serial; (void)surface; (void)keys;
	kbfocus = 1;
}
static void kb_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
		struct wl_surface *surface) {
	(void)data; (void)kb; (void)serial; (void)surface;
	/* lost keyboard focus -> user clicked/focused another window */
	if (kbfocus) quit = 1;
	kbfocus = 0;
}
static void kb_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
		uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp) {
	(void)data; (void)kb; (void)serial;
	if (xkb_state) xkb_state_update_mask(xkb_state, dep, lat, lock, 0, 0, grp);
}
static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial,
		uint32_t time, uint32_t key, uint32_t state) {
	(void)data; (void)kb; (void)serial; (void)time;
	if (!xkb_state) return;

	xkb_keycode_t kc = (xkb_keycode_t)key + 8;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(xkb_state, kc);

	if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
		if (key == repeat_key) { repeat_active = 0; repeat_key = 0; }
		return;
	}
	/* PRESSED: ignore the compositor's own repeats; we drive repeat. */
	if (key == repeat_key) return;

	key_action(sym, kc);

	/* start auto-repeat for all keys except the one-shot Enter/Escape */
	if (sym != XKB_KEY_Return && sym != XKB_KEY_KP_Enter && sym != XKB_KEY_Escape
			&& repeat_rate > 0) {
		repeat_key = key;
		repeat_kc = kc;
		repeat_sym = sym;
		repeat_active = 1;
		repeat_at = now_ms() + repeat_delay;
	}
}
static void kb_repeat_info(void *data, struct wl_keyboard *kb,
		int32_t rate, int32_t delay) {
	(void)data; (void)kb;
	if (rate > 0)  repeat_rate = rate;
	if (delay > 0) repeat_delay = delay;
}
static const struct wl_keyboard_listener kbd_listener = {
	.keymap = kb_keymap, .enter = kb_enter, .leave = kb_leave,
	.key = kb_key, .modifiers = kb_modifiers, .repeat_info = kb_repeat_info
};

/* ----- pointer ----- */

/* Update the hovered selection from the pointer position.
 * Returns 1 only if the highlighted entry changed (so a redraw is needed). */
static int update_hover(void) {
	int nh;
	if (ptr_y < HEADER_H + 4) {
		nh = -1;
	} else {
		int row = (ptr_y - HEADER_H) / ROW_H;
		if (row < 0) row = 0;
		if (row >= VISIBLE) row = VISIBLE - 1;
		int idx = scroll + row;
		nh = (idx >= 0 && idx < nfiltered) ? idx : -1;
	}
	if (nh == hover) return 0;
	hover = nh;
	if (hover >= 0) sel = hover;
	return 1;
}
static void ptr_enter(void *data, struct wl_pointer *p, uint32_t serial,
		struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	(void)data; (void)p; (void)surface; (void)serial;
	entered = 1;
	ptr_x = wl_fixed_to_int(sx);
	ptr_y = wl_fixed_to_int(sy);
	if (update_hover()) dirty = 1;
}
static void ptr_leave(void *data, struct wl_pointer *p, uint32_t serial,
		struct wl_surface *surface) {
	(void)data; (void)p; (void)serial; (void)surface;
	if (hover != -1) { hover = -1; dirty = 1; }
	entered = 0;
}
static void ptr_motion(void *data, struct wl_pointer *p, uint32_t time,
		wl_fixed_t sx, wl_fixed_t sy) {
	(void)data; (void)p; (void)time;
	ptr_x = wl_fixed_to_int(sx);
	ptr_y = wl_fixed_to_int(sy);
	if (update_hover()) dirty = 1;
}
static void ptr_button(void *data, struct wl_pointer *p, uint32_t serial,
		uint32_t time, uint32_t button, uint32_t state) {
	(void)data; (void)p; (void)serial; (void)time;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED && button == BTN_LEFT) {
		update_hover();
		if (hover >= 0 && hover < nfiltered)
			run_app(filtered[hover]);
	}
}
/* Scroll the list by `steps` notches (negative = up). The selection tracks
 * the viewport (render() keeps it in view) and we only redraw on change. */
static void apply_scroll(int steps) {
	if (nfiltered == 0) return;
	int old = sel;
	sel += steps;
	if (sel < 0) sel = 0;
	if (sel > nfiltered - 1) sel = nfiltered - 1;
	if (sel != old) dirty = 1;
}

static void ptr_axis(void *data, struct wl_pointer *p, uint32_t time,
		uint32_t axis, wl_fixed_t value) {
	(void)data; (void)p; (void)time;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		wheel_cont += wl_fixed_to_double(value);
}
static void ptr_frame(void *data, struct wl_pointer *p) {
	(void)data; (void)p;
	int steps;
	if (wheel_disc != 0) {                  /* mice / v120 API: exact notches */
		steps = wheel_disc;
	} else if (wheel_cont != 0.0) {         /* trackpad / continuous fallback */
		double total = wheel_cont / SCROLL_UNIT + wheel_carry;
		steps = (int)total;
		wheel_carry = total - steps;        /* keep remainder for next frame */
		if (wheel_carry >  1.0) wheel_carry =  1.0;
		if (wheel_carry < -1.0) wheel_carry = -1.0;
	} else {
		steps = 0;
	}
	wheel_disc = 0;
	wheel_cont = 0.0;
	if (steps != 0) apply_scroll(steps);
}
static void ptr_axis_source(void *data, struct wl_pointer *p, uint32_t src) {
	(void)data; (void)p; (void)src;
}
static void ptr_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis) {
	(void)data; (void)p; (void)time; (void)axis;
}
static void ptr_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t d) {
	(void)data; (void)p;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		wheel_disc += d;
}
static void ptr_axis_value120(void *data, struct wl_pointer *p, uint32_t axis, int32_t v) {
	(void)data; (void)p;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
		wheel_disc += v / 120;
}
static void ptr_axis_relative_direction(void *data, struct wl_pointer *p,
		uint32_t axis, uint32_t dir) {
	(void)data; (void)p; (void)axis; (void)dir;
}
static const struct wl_pointer_listener pointer_listener = {
	.enter = ptr_enter,
	.leave = ptr_leave,
	.motion = ptr_motion,
	.button = ptr_button,
	.axis = ptr_axis,
	.frame = ptr_frame,
	.axis_source = ptr_axis_source,
	.axis_stop = ptr_axis_stop,
	.axis_discrete = ptr_axis_discrete,
	.axis_value120 = ptr_axis_value120,
	.axis_relative_direction = ptr_axis_relative_direction,
};

void input_init(void) {
	if (!seat) return;
	wl_seat_add_listener(seat, &seat_listener, NULL);
	if (!kbd) {
		kbd = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(kbd, &kbd_listener, NULL);
	}
	if (!pointer) {
		pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(pointer, &pointer_listener, NULL);
	}
}
