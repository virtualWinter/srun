// render.c - cairo rendering of the launcher window.

#include "srun.h"

#include <time.h>
#include <string.h>
#include <strings.h>

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
	int rr = theme.radius;
	round_rect(c, 0, 0, W, H, rr);
	set_rgba(c, &theme.bg);
	cairo_fill(c);
	set_rgba(c, &theme.border);
	cairo_set_line_width(c, 2);
	cairo_stroke(c);

	/* prompt — shows '!' in bang mode, '#' in terminal-command mode,
	 * '>' in normal app-launcher mode, or field info in config mode. */
	int bang_mode = (input_len > 0 && input[0] == '!');
	int term_mode = (input_len > 0 && input[0] == '#');
	cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(c, 20);

	if (config_mode == 2) {
		/* Editing a field: show "<label>: <value>" as the prompt. */
		set_rgba(c, &theme.term);
		cairo_move_to(c, MARGIN, HEADER_H / 2 + 7);
		cairo_show_text(c, config_field_label(config_edit_idx));
		cairo_show_text(c, ": ");
		set_rgba(c, &theme.title);
		cairo_show_text(c, input);
		cairo_text_extents_t ex;
		cairo_text_extents(c, input, &ex);
		if (time(NULL) % 2 == 0) {
			set_rgba(c, &theme.caret);
			cairo_rectangle(c, MARGIN + ex.x_advance + 1,
				HEADER_H / 2 - 9, 2, 18);
			cairo_fill(c);
		}
	} else if (config_mode == 1) {
		/* Browsing fields: show "! swm" as the prompt. */
		set_rgba(c, &theme.term);
		cairo_move_to(c, MARGIN, HEADER_H / 2 + 7);
		cairo_show_text(c, "!");
		set_rgba(c, &theme.title);
		cairo_show_text(c, " swm");

		/* Changed indicator (orange dot when there are unsaved changes). */
		if (config_is_modified()) {
			cairo_set_source_rgb(c, 1, 0.6, 0);
			cairo_arc(c, W - MARGIN - 16, HEADER_H / 2 - 2, 4, 0, 2 * M_PI);
			cairo_fill(c);
		}
	} else if (config_mode == 3) {
		/* Colour‑picker header: preview swatch + hex + field label. */
		cairo_select_font_face(c, "sans-serif",
			CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(c, 20);
		/* Preview swatch. */
		cairo_set_source_rgb(c, cp_r/255.0f, cp_g/255.0f, cp_b/255.0f);
		round_rect(c, MARGIN, 4, 48, HEADER_H - 8, 6);
		cairo_fill_preserve(c);
		cairo_set_source_rgba(c, 0.2, 0.2, 0.2, 0.4);
		cairo_set_line_width(c, 1);
		cairo_stroke(c);
		/* Hex value next to swatch. */
		char hbuf[16];
		snprintf(hbuf, sizeof hbuf, "#%02x%02x%02x", cp_r, cp_g, cp_b);
		set_rgba(c, &theme.title);
		cairo_move_to(c, MARGIN + 60, HEADER_H / 2 + 7);
		cairo_show_text(c, hbuf);
		/* Field label on the right. */
		set_rgba(c, &theme.term);
		cairo_move_to(c, W - 280, HEADER_H / 2 + 7);
		cairo_show_text(c, config_field_label(config_edit_idx));
	} else if (bang_mode) {
		char pfx[2] = {'!', 0};
		const char *q = input + 1;
		cairo_move_to(c, MARGIN, HEADER_H / 2 + 7);
		set_rgba(c, &theme.term);
		cairo_show_text(c, pfx);
		cairo_show_text(c, " ");
		set_rgba(c, &theme.title);
		cairo_show_text(c, q);
	} else if (term_mode) {
		char pfx[2] = {'#', 0};
		const char *q = input + 1;
		cairo_move_to(c, MARGIN, HEADER_H / 2 + 7);
		set_rgba(c, &theme.term);
		cairo_show_text(c, pfx);
		cairo_show_text(c, " ");
		set_rgba(c, &theme.title);
		cairo_show_text(c, q);
	} else {
		char pfx[2] = {'>', 0};
		const char *q = input;
		cairo_move_to(c, MARGIN, HEADER_H / 2 + 7);
		set_rgba(c, &theme.title);
		cairo_show_text(c, pfx);
		cairo_show_text(c, " ");
		cairo_show_text(c, q);
		/* blinking caret in normal mode */
		char prompt[300];
		snprintf(prompt, sizeof prompt, "> %s", q);
		cairo_text_extents_t ex;
		cairo_text_extents(c, prompt, &ex);
		if (time(NULL) % 2 == 0) {
			set_rgba(c, &theme.caret);
			cairo_rectangle(c, MARGIN + ex.x_advance + 1,
				HEADER_H / 2 - 9, 2, 18);
			cairo_fill(c);
		}
	}

	/* separator */
	set_rgba(c, &theme.sep);
	cairo_set_line_width(c, 1);
	cairo_move_to(c, MARGIN, HEADER_H);
	cairo_line_to(c, W - MARGIN, HEADER_H);
	cairo_stroke(c);

	/* list */
	cairo_select_font_face(c, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(c, 15);

	if (config_mode == 3) {
		/* ─── Colour‑picker below header ─── */
		int PAD = 8;
		int avail_h = (H - HEADER_H) - 2 * PAD;
		int stripe_w = 18;
		int gap = 6;
		int sq_size = avail_h;
		int sq_top = HEADER_H + PAD;
		int stripe_x = PAD;
		int sq_x = PAD + stripe_w + gap;

		/* Right info panel */
		int pan_x = sq_x + sq_size + gap;
		int pan_w = W - PAD - pan_x;

		/* ── Draw the hue strip ── */
		cairo_select_font_face(c, "sans-serif",
			CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
		cairo_set_font_size(c, 12);
		for (int y = 0; y < sq_size; y++) {
			float hue = 360.0f * y / sq_size;
			float ch = 1.0f, hp = hue / 60.0f;
			float xc = ch * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
			float rf, gf, bf;
			switch (((int)hp) % 6) {
				case 0: rf = ch; gf = xc; bf = 0; break;
				case 1: rf = xc; gf = ch; bf = 0; break;
				case 2: rf = 0; gf = ch; bf = xc; break;
				case 3: rf = 0; gf = xc; bf = ch; break;
				case 4: rf = xc; gf = 0; bf = ch; break;
				default: rf = ch; gf = 0; bf = xc; break;
			}
			cairo_set_source_rgb(c, rf, gf, bf);
			cairo_rectangle(c, stripe_x, sq_top + y, stripe_w, 1);
			cairo_fill(c);
		}

		/* Hue marker on the hue strip. */
		int hue_mark_y = sq_top + (int)(cp_hue * sq_size / 360.0f);
		cairo_set_source_rgb(c, 1, 1, 1);
		cairo_set_line_width(c, 2);
		cairo_move_to(c, stripe_x - 1, hue_mark_y);
		cairo_line_to(c, stripe_x + stripe_w + 1, hue_mark_y);
		cairo_stroke(c);
		cairo_set_source_rgb(c, 0, 0, 0);
		cairo_set_line_width(c, 1);
		cairo_move_to(c, stripe_x - 1, hue_mark_y);
		cairo_line_to(c, stripe_x + stripe_w + 1, hue_mark_y);
		cairo_stroke(c);

		/* ── Draw the saturation × value square ── */
		cairo_surface_t *sq_surf = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, sq_size, sq_size);
		unsigned char *sq_data = cairo_image_surface_get_data(sq_surf);
		int sq_stride = cairo_image_surface_get_stride(sq_surf);

		for (int y = 0; y < sq_size; y++) {
			uint32_t *row = (uint32_t *)(sq_data + y * sq_stride);
			float v = 1.0f - (float)y / sq_size;
			for (int x = 0; x < sq_size; x++) {
				float s = (float)x / sq_size;
				float C = cp_val * s * v;
				float Hp = cp_hue / 60.0f;
				float X = C * (1.0f - fabsf(fmodf(Hp, 2.0f) - 1.0f));
				float m = cp_val * v - C;
				float rf, gf, bf;
				int ip = ((int)Hp) % 6;
				switch (ip) {
					case 0: rf = C; gf = X; bf = 0; break;
					case 1: rf = X; gf = C; bf = 0; break;
					case 2: rf = 0; gf = C; bf = X; break;
					case 3: rf = 0; gf = X; bf = C; break;
					case 4: rf = X; gf = 0; bf = C; break;
					default: rf = C; gf = 0; bf = X; break;
				}
				int ri = (int)((rf + m) * 255 + 0.5f);
				int gi = (int)((gf + m) * 255 + 0.5f);
				int bi = (int)((bf + m) * 255 + 0.5f);
				if (ri < 0) ri = 0;
				if (ri > 255) ri = 255;
				if (gi < 0) gi = 0;
				if (gi > 255) gi = 255;
				if (bi < 0) bi = 0;
				if (bi > 255) bi = 255;
				row[x] = (0xFFu << 24) | (ri << 16) | (gi << 8) | bi;
			}
		}
		cairo_surface_mark_dirty(sq_surf);

		cairo_save(c);
		cairo_set_source_surface(c, sq_surf, sq_x, sq_top);
		cairo_paint(c);
		cairo_restore(c);
		cairo_surface_destroy(sq_surf);

		/* Square border. */
		set_rgba(c, &theme.sep);
		cairo_set_line_width(c, 1);
		cairo_rectangle(c, sq_x, sq_top, sq_size, sq_size);
		cairo_stroke(c);

		/* ── Crosshair ── */
		int cx = sq_x + (int)(cp_sat * sq_size);
		int cy = sq_top + sq_size - (int)(cp_val * sq_size);
		cairo_set_source_rgb(c, 1, 1, 1);
		cairo_set_line_width(c, 2);
		cairo_arc(c, cx, cy, 7, 0, 2 * M_PI);
		cairo_stroke(c);
		cairo_set_source_rgb(c, 0, 0, 0);
		cairo_set_line_width(c, 1);
		cairo_arc(c, cx, cy, 6, 0, 2 * M_PI);
		cairo_stroke(c);
		cairo_set_source_rgb(c, 1, 1, 1);
		cairo_arc(c, cx, cy, 2, 0, 2 * M_PI);
		cairo_fill(c);

		/* ── Right info panel ── */
		cairo_select_font_face(c, "sans-serif",
			CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);

		/* Large preview swatch. */
		int sw_x = pan_x + (pan_w - 70) / 2;
		int sw_y = sq_top + 30;
		cairo_set_source_rgb(c, cp_r/255.0f, cp_g/255.0f, cp_b/255.0f);
		round_rect(c, sw_x, sw_y, 70, 50, 6);
		cairo_fill_preserve(c);
		cairo_set_source_rgba(c, 0.2, 0.2, 0.2, 0.4);
		cairo_set_line_width(c, 1);
		cairo_stroke(c);

		/* Hex value below swatch. */
		char hbuf[16];
		snprintf(hbuf, sizeof hbuf, "#%02x%02x%02x", cp_r, cp_g, cp_b);
		set_rgba(c, &theme.title);
		cairo_set_font_size(c, 18);
		cairo_text_extents_t wex;
		cairo_text_extents(c, hbuf, &wex);
		cairo_move_to(c, pan_x + (pan_w - wex.width) / 2, sw_y + 80);
		cairo_show_text(c, hbuf);

		/* R, G, B values. */
		char rgb_lines[3][16];
		snprintf(rgb_lines[0], sizeof rgb_lines[0], "R  %d", cp_r);
		snprintf(rgb_lines[1], sizeof rgb_lines[1], "G  %d", cp_g);
		snprintf(rgb_lines[2], sizeof rgb_lines[2], "B  %d", cp_b);
		cairo_set_font_size(c, 15);
		int lx = pan_x + (pan_w - 70) / 2;
		int ly = sw_y + 110;
		float col[3][3] = {
			{1, 0.2f, 0.2f}, {0.2f, 1, 0.2f}, {0.2f, 0.2f, 1}
		};
		for (int i = 0; i < 3; i++) {
			cairo_set_source_rgb(c, col[i][0], col[i][1], col[i][2]);
			cairo_move_to(c, lx, ly + i * 22);
			cairo_show_text(c, rgb_lines[i]);
		}

		/* Field label + keyboard hints at bottom of panel. */
		cairo_set_font_size(c, 12);
		set_rgba(c, &theme.hint);
		cairo_move_to(c, pan_x, H - PAD - 4);
		cairo_show_text(c, "Enter=ok  Esc=cancel");
	} else {
		/* Normal, bang, term, or config‑browse list. */
		int text_x = (term_mode || bang_mode || config_mode) ? MARGIN : MARGIN + ICON + 8;
		int rcol_x = W - MARGIN - 180;  /* right‑column X for value text */
		int shown = nfiltered < VISIBLE ? nfiltered : VISIBLE;

		/* keep the selected row in view */
		if (sel < scroll) scroll = sel;
		if (sel >= scroll + VISIBLE) scroll = sel - VISIBLE + 1;
		if (scroll < 0) scroll = 0;

		for (int i = 0; i < shown; i++) {
			int idx = scroll + i;
			if (idx < 0 || idx >= nfiltered) continue;
			int y = HEADER_H + i * ROW_H;
			App *a = filtered[idx];
			if (idx == sel) {
				int sr = rr > 6 ? 6 : rr / 2;
				round_rect(c, MARGIN / 2, y + 3, W - MARGIN, ROW_H - 6, sr);
				set_rgba(c, &theme.sel);
				cairo_fill(c);
			}
			/* icon: loaded and cached on first paint */
			if (!config_mode && a->icon && !a->icon_surf)
				a->icon_surf = load_icon(a->icon);
			if (!config_mode && a->icon_surf)
				draw_icon(c, a->icon_surf, MARGIN, y + (ROW_H - ICON) / 2);

			if (config_mode == 1) {
				/* --- Config browse mode: label | value --- */
				int fi = config_find_idx(a->exec);

				/* Left column: label. */
				const Color *lc = (idx == sel) ? &theme.label : &theme.text;
				set_rgba(c, lc);
				cairo_move_to(c, text_x, y + 20);
				cairo_show_text(c, a->name);

				/* Right column: value with type‑specific rendering. */
				if (fi >= 0) {
					int ftype = config_field_type(fi);
					const char *val = a->desktop ? a->desktop : "";

					if (ftype == 1 /* FLD_BOOL */ && val[0]) {
						int on = val[0] == 't' || val[0] == '1';
						int bx = rcol_x, bw = 46, bh = 20;
						int by = y + (ROW_H - bh) / 2;
						round_rect(c, bx, by, bw, bh, 10);
						if (on) cairo_set_source_rgb(c, 0.25, 0.65, 0.25);
						else    cairo_set_source_rgb(c, 0.55, 0.55, 0.55);
						cairo_fill(c);
						cairo_set_source_rgb(c, 1, 1, 1);
						cairo_select_font_face(c, "sans-serif",
							CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
						cairo_set_font_size(c, 11);
						cairo_move_to(c, bx + (bw - 22) / 2, by + 14);
						cairo_show_text(c, on ? "ON" : "OFF");
						cairo_set_font_size(c, 15);
						cairo_select_font_face(c, "sans-serif",
							CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
					} else if (ftype == 3 /* FLD_COLOR */ && val[0] == '#') {
						float cr_, cg_, cb_;
						unsigned int rv, gv, bv;
						if (sscanf(val + 1, "%2x%2x%2x", &rv, &gv, &bv) == 3) {
							cr_ = rv / 255.0f;
							cg_ = gv / 255.0f;
							cb_ = bv / 255.0f;
							int sx = rcol_x, sw = 18, sh = 18;
							round_rect(c, sx, y + (ROW_H - sh) / 2, sw, sh, 4);
							cairo_set_source_rgb(c, cr_, cg_, cb_);
							cairo_fill_preserve(c);
							cairo_set_source_rgba(c, 0.2, 0.2, 0.2, 0.5);
							cairo_set_line_width(c, 1);
							cairo_stroke(c);
							set_rgba(c, &theme.value);
							cairo_move_to(c, sx + sw + 6, y + 20);
							cairo_show_text(c, val);
						}
					} else {
						set_rgba(c, &theme.value);
						cairo_move_to(c, rcol_x, y + 20);
						cairo_show_text(c, val);
					}
				}
			} else {
				/* --- Normal / bang / term mode --- */
				const Color *lc = (idx == sel) ? &theme.label : &theme.text;
				set_rgba(c, lc);
				cairo_move_to(c, text_x, y + 20);
				cairo_show_text(c, a->name);
				if (bang_mode) {
					set_rgba(c, &theme.value);
					cairo_move_to(c, text_x + 220, y + 20);
					cairo_show_text(c, a->desktop ? a->desktop : "");
				} else if (!term_mode) {
					set_rgba(c, &theme.value);
					cairo_move_to(c, text_x + 200, y + 20);
					cairo_show_text(c, a->exec);
				}
			}
		}

		if (nfiltered == 0) {
			set_rgba(c, &theme.hint);
			cairo_move_to(c, MARGIN, HEADER_H + ROW_H / 2 + 6);
			cairo_show_text(c, "no matches");
		}
	}

	cairo_surface_flush(csurf);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, W, H);
	wl_surface_commit(surface);
}
