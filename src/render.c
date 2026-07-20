// render.c - cairo rendering of the launcher window.

#include "srun.h"

#include <time.h>

static void round_rect(cairo_t *c, double x, double y, double w, double h, double r) {
	cairo_new_path(c);
	cairo_move_to(c, x + r, y);
	cairo_arc(c, x + w - r, y + r, r, -M_PI / 2, 0);
	cairo_arc(c, x + w - r, y + h - r, r, 0, M_PI / 2);
	cairo_arc(c, x + r, y + h - r, r, M_PI / 2, M_PI);
	cairo_arc(c, x + r, y + r, r, M_PI, M_PI * 1.5);
	cairo_close_path(c);
}

static void draw_icon(cairo_t *c, cairo_surface_t *s, int x, int y) {
	double iw = cairo_image_surface_get_width(s);
	double ih = cairo_image_surface_get_height(s);
	double sc = (double)ICON / (iw > ih ? iw : ih);
	cairo_save(c);
	cairo_translate(c, x, y);
	cairo_scale(c, sc, sc);
	cairo_set_source_surface(c, s, 0, 0);
	cairo_paint(c);
	cairo_restore(c);
}

void render(void) {
	cairo_t *c = cr;

	cairo_save(c);
	cairo_set_operator(c, CAIRO_OPERATOR_CLEAR);
	cairo_paint(c);
	cairo_restore(c);
	cairo_set_operator(c, CAIRO_OPERATOR_OVER);

	/* panel background + border */
	round_rect(c, 0, 0, W, H, 14);
	cairo_set_source_rgba(c, 0.10, 0.10, 0.14, 0.96);
	cairo_fill(c);
	cairo_set_source_rgba(c, 0.53, 0.71, 0.98, 1.0);
	cairo_set_line_width(c, 2);
	cairo_stroke(c);

	/* prompt — shows '#' instead of '>' in terminal-command mode */
	int term_mode = (input_len > 0 && input[0] == '#');
	cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(c, 20);
	char pfx[2] = { term_mode ? '#' : '>', '\0' };
	const char *q = term_mode ? input + 1 : input;
	char prompt[300];
	snprintf(prompt, sizeof prompt, "%s %s", pfx, q);
	cairo_move_to(c, MARGIN, HEADER_H / 2 + 7);
	cairo_set_source_rgba(c, term_mode ? 1.0 : 0.92, term_mode ? 0.66 : 0.94,
	                         term_mode ? 0.36 : 0.98, 1.0);
	cairo_show_text(c, pfx);
	cairo_show_text(c, " ");
	cairo_set_source_rgba(c, 0.92, 0.94, 0.98, 1.0);
	cairo_show_text(c, q);

	/* blinking caret */
	cairo_text_extents_t ex;
	cairo_text_extents(c, prompt, &ex);
	if (time(NULL) % 2 == 0) {
		cairo_set_source_rgba(c, 0.85, 0.90, 1.0, 1.0);
		cairo_rectangle(c, MARGIN + ex.x_advance + 1, HEADER_H / 2 - 9, 2, 18);
		cairo_fill(c);
	}

	/* separator */
	cairo_set_source_rgba(c, 0.35, 0.37, 0.45, 0.7);
	cairo_set_line_width(c, 1);
	cairo_move_to(c, MARGIN, HEADER_H);
	cairo_line_to(c, W - MARGIN, HEADER_H);
	cairo_stroke(c);

	/* list */
	cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(c, 15);
	/* keep the selected row in view */
	if (sel < scroll) scroll = sel;
	if (sel >= scroll + VISIBLE) scroll = sel - VISIBLE + 1;
	if (scroll < 0) scroll = 0;

	int text_x = term_mode ? MARGIN : MARGIN + ICON + 8;
	int shown = nfiltered < VISIBLE ? nfiltered : VISIBLE;
	for (int i = 0; i < shown; i++) {
		int idx = scroll + i;
		if (idx < 0 || idx >= nfiltered) continue;
		int y = HEADER_H + i * ROW_H;
		App *a = filtered[idx];
		if (idx == sel) {
			round_rect(c, MARGIN / 2, y + 3, W - MARGIN, ROW_H - 6, 6);
			cairo_set_source_rgba(c, 0.53, 0.71, 0.98, 0.22);
			cairo_fill(c);
		}
		/* icon: loaded and cached on first paint */
		if (a->icon && !a->icon_surf) a->icon_surf = load_icon(a->icon);
		if (a->icon_surf) draw_icon(c, a->icon_surf, MARGIN, y + (ROW_H - ICON) / 2);
		cairo_set_source_rgba(c, idx == sel ? 0.98 : 0.82,
		                         idx == sel ? 0.99 : 0.84,
		                         idx == sel ? 1.0 : 0.88, 1.0);
		cairo_move_to(c, text_x, y + 20);
		cairo_show_text(c, a->name);
		if (!term_mode) {
			cairo_set_source_rgba(c, 0.55, 0.57, 0.65, 1.0);
			cairo_move_to(c, text_x + 200, y + 20);
			cairo_show_text(c, a->exec);
		}
	}
	if (nfiltered == 0) {
		cairo_set_source_rgba(c, 0.6, 0.62, 0.7, 1.0);
		cairo_move_to(c, MARGIN, HEADER_H + ROW_H / 2 + 6);
		cairo_show_text(c, "no matches");
	}

	cairo_surface_flush(csurf);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, W, H);
	wl_surface_commit(surface);
}
