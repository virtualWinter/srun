// config_sub.c — 3rd-level config sub-menu for srun.
//
// Activated by typing `!swm` → Enter.  The normal app list is replaced by
// the 6 swm.conf settings fields.  Each field looks like an ordinary list
// item.  Enter on a non‑mod field starts editing (the input buffer becomes
// the new value), Enter on a mod field cycles the value.  Escape goes back
// to the !‑menu.  Ctrl+S saves and signals swm.

#include "srun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

/* =================================================================
 * Field model
 * ================================================================= */

enum fld_type { FLD_MOD, FLD_BOOL, FLD_NUM, FLD_COLOR, FLD_PATH };

typedef struct {
	const char *key;
	const char *label;
	char         value[256];
	char         saved[256];     /* value on disk (to detect changes) */
	enum fld_type type;
} CfgField;

static CfgField cfields[] = {
	{ "mod",           "Modifier",       "SUPER",   "", FLD_MOD   },
	{ "border_width",  "Border width",   "1",       "", FLD_NUM   },
	{ "gap",           "Top gap",        "0",       "", FLD_NUM   },
	{ "border_normal", "Border normal",  "#333333", "", FLD_COLOR },
	{ "border_select", "Border focused", "#c831dc", "", FLD_COLOR },
	{ "wallpaper",     "Wallpaper",      "",         "", FLD_PATH  },
};
static int ncfg = sizeof(cfields) / sizeof(cfields[0]);

int  config_mode = 0;
int  config_edit_idx = 0;
int  config_picker_active = 0;  /* non-zero while waiting for file-picker child */

/* ----- helpers ----- */

int config_nfields(void) { return ncfg; }
const char *config_field_label(int i) {
	return (i >= 0 && i < ncfg) ? cfields[i].label : "";
}
const char *config_field_value(int i) {
	return (i >= 0 && i < ncfg) ? cfields[i].value : "";
}
const char *config_field_key(int i) {
	return (i >= 0 && i < ncfg) ? cfields[i].key   : "";
}
int config_field_type(int i) {
	return (i >= 0 && i < ncfg) ? (int)cfields[i].type : -1;
}

static const char *cfg_dir(void) {
	const char *xc = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	static char buf[PATH_MAX];
	if (xc && *xc) snprintf(buf, sizeof buf, "%s/swm", xc);
	else if (home && *home) snprintf(buf, sizeof buf, "%s/.config/swm", home);
	else snprintf(buf, sizeof buf, ".config/swm");
	return buf;
}

static char *trim(char *s) {
	while (*s && isspace((unsigned char)*s)) s++;
	if (!*s) return s;
	char *e = s + strlen(s) - 1;
	while (e > s && isspace((unsigned char)*e)) *e-- = 0;
	return s;
}

/* =================================================================
 * File‑picker dialog (for FLD_PATH fields)
 * ================================================================= */

/* Fork a zenity/kdialog file‑selection dialog, wait for it, and return the
 * chosen path (caller must free).  Returns NULL if cancelled or unavailable. */
static char *pick_file_dialog(void) {
	int pipes[2];
	if (pipe(pipes) < 0) return NULL;

	pid_t pid = fork();
	if (pid == 0) {
		/* Child — redirect stdout to pipe. */
		close(pipes[0]);
		dup2(pipes[1], STDOUT_FILENO);
		close(pipes[1]);
		/* Try zenity first. */
		execlp("zenity", "zenity", "--file-selection",
			"--file-filter", "Images *.png *.jpg *.jpeg",
			"--title", "Select Wallpaper",
			(char *)NULL);
		/* Fallback to kdialog. */
		execlp("kdialog", "kdialog", "--getopenfilename", "", (char *)NULL);
		/* Nōthing available. */
		_exit(2);
	}

	close(pipes[1]);

	/* Poll with WNOHANG while dispatching Wayland events so the
	 * compositor's pings are answered during the multi-second file
	 * dialog.  Without this the compositor would disconnect srun for
	 * failing to respond to pings. */
	int status;
	char buf[4096] = {0};
	ssize_t n = 0;
	while (waitpid(pid, &status, WNOHANG) != pid) {
		/* Process incoming Wayland events (pings, etc.) so the
		 * compositor doesn't disconnect us. */
		if (wl_display_dispatch(display) < 0) break;
		usleep(50000);  /* 50ms — yield CPU so the compositor & dialog run */
	}

	n = read(pipes[0], buf, sizeof(buf) - 1);
	close(pipes[0]);

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && n > 0) {
		/* Strip trailing newline. */
		char *nl = strchr(buf, '\n');
		if (nl) *nl = 0;
		return strlen(buf) > 0 ? strdup(buf) : NULL;
	}
	return NULL;  /* cancelled or no dialog available */
}

/* =================================================================
 * Colour picker — classic HSV square
 * ================================================================= */

/* State shared with render.c and input.c. */
int   cp_r, cp_g, cp_b;        /* 0‑255 (computed from HSV) */
float cp_hue;                   /* 0‑360 */
float cp_sat;                   /* 0‑1 */
float cp_val;                   /* 0‑1 */
char  cp_hex[16];               /* hex‑field input buffer */
int   cp_hex_len;
int   cp_drag;                  /* which element the cursor is on: 0=square, 1=hue-strip */

/* Parse a #rrggbb string into RGB. */
int parse_hex_color(const char *hex, int *r, int *g, int *b) {
	unsigned int rv, gv, bv;
	if (hex && hex[0] == '#' &&
	    sscanf(hex + 1, "%2x%2x%2x", &rv, &gv, &bv) == 3) {
		*r = (int)rv; *g = (int)gv; *b = (int)bv;
		return 1;
	}
	return 0;
}

/* Convert RGB → HSV (h=0‑360, s=0‑1, v=0‑1). */
void rgb_to_hsv(int r, int g, int b, float *h, float *s, float *v) {
	float rf = r / 255.0f, gf = g / 255.0f, bf = b / 255.0f;
	float cmax = rf, cmin = rf;
	if (gf > cmax) cmax = gf;
	if (bf > cmax) cmax = bf;
	if (gf < cmin) cmin = gf;
	if (bf < cmin) cmin = bf;
	float delta = cmax - cmin;
	if (delta == 0) *h = 0;
	else if (cmax == rf) *h = 60.0f * fmodf((gf - bf) / delta, 6.0f);
	else if (cmax == gf) *h = 60.0f * ((bf - rf) / delta + 2.0f);
	else                 *h = 60.0f * ((rf - gf) / delta + 4.0f);
	if (*h < 0) *h += 360.0f;
	*s = (cmax == 0) ? 0 : delta / cmax;
	*v = cmax;
}

/* Convert HSV → RGB and store in cp_r/cp_g/cp_b. */
void hsv_to_rgb(float h, float s, float v) {
	float c = v * s;
	float hp = h / 60.0f;
	float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
	float m = v - c;
	float rf, gf, bf;
	int ip = (int)hp % 6;
	switch (ip) {
		case 0: rf = c; gf = x; bf = 0; break;
		case 1: rf = x; gf = c; bf = 0; break;
		case 2: rf = 0; gf = c; bf = x; break;
		case 3: rf = 0; gf = x; bf = c; break;
		case 4: rf = x; gf = 0; bf = c; break;
		default: rf = c; gf = 0; bf = x; break;
	}
	cp_r = (int)((rf + m) * 255.0f + 0.5f);
	cp_g = (int)((gf + m) * 255.0f + 0.5f);
	cp_b = (int)((bf + m) * 255.0f + 0.5f);
	if (cp_r > 255) cp_r = 255;
	if (cp_r < 0) cp_r = 0;
	if (cp_g > 255) cp_g = 255;
	if (cp_g < 0) cp_g = 0;
	if (cp_b > 255) cp_b = 255;
	if (cp_b < 0) cp_b = 0;
}

/* Enter colour‑picker mode (config_mode = 3). */
void config_enter_color_picker(void) {
	if (config_edit_idx < 0 || config_edit_idx >= ncfg) return;
	if (!parse_hex_color(cfields[config_edit_idx].value, &cp_r, &cp_g, &cp_b))
		cp_r = cp_g = cp_b = 128;
	rgb_to_hsv(cp_r, cp_g, cp_b, &cp_hue, &cp_sat, &cp_val);
	cp_hex[0] = 0;
	cp_hex_len = 0;
	cp_drag = 0;
	config_mode = 3;
	nfiltered = 0;
	free(filtered);
	filtered = NULL;
	input[0] = 0;
	input_len = 0;
	dirty = 1;
}

/* Save config (if modified) — config_save_all writes + signals swm. */
static void config_save_and_signal(void) {
	if (config_is_modified())
		config_save_all();
}

/* Commit the current RGB back to the config field, save, signal, return. */
void config_commit_color_picker(void) {
	if (config_edit_idx < 0 || config_edit_idx >= ncfg) return;
	snprintf(cfields[config_edit_idx].value,
		sizeof cfields[config_edit_idx].value,
		"#%02x%02x%02x", cp_r, cp_g, cp_b);
	config_update_field_names();
	config_save_and_signal();
	config_mode = 1;
	nfiltered = 0;
	free(filtered);
	filtered = NULL;
	snprintf(input, sizeof input, "!swm ");
	input_len = (int)strlen(input);
	rebuild();
	dirty = 1;
}

/* =================================================================
 * Per‑field Enter handling
 * ================================================================= */

/* Handle Enter on a field in browse mode.  Returns 1 if the field was
 * handled directly (mod cycle, bool toggle, file/color picker) and the
 * caller should NOT enter text‑editing mode.  Returns 0 if the caller
 * should enter text‑editing mode (NUM). */
int config_handle_enter(int idx) {
	if (idx < 0 || idx >= ncfg) return 0;

	if (cfields[idx].type == FLD_MOD) {
		cycle_mod(idx);
		config_update_field_names();
		config_save_and_signal();
		dirty = 1;
		return 1;
	}

	if (cfields[idx].type == FLD_BOOL) {
		int on = !strcasecmp(cfields[idx].value, "true")
		      || !strcmp(cfields[idx].value, "1")
		      || !strcasecmp(cfields[idx].value, "on")
		      || !strcasecmp(cfields[idx].value, "yes");
		snprintf(cfields[idx].value, sizeof cfields[idx].value,
			"%s", on ? "false" : "true");
		config_update_field_names();
		config_save_and_signal();
		dirty = 1;
		return 1;
	}

	if (cfields[idx].type == FLD_COLOR) {
		config_enter_color_picker();
		return 1;
	}

	if (cfields[idx].type == FLD_PATH) {
		char *chosen = pick_file_dialog();
		if (chosen) {
		snprintf(cfields[idx].value, sizeof cfields[idx].value,
				"%s", chosen);
			free(chosen);
			config_update_field_names();
			config_save_and_signal();
			dirty = 1;
		}
		return 1;
	}

	/* NUM → enter text‑editing mode. */
	return 0;
}

/* =================================================================
 * Cycling a MOD field (also used from input.c)
 * ================================================================= */

static const char *mod_opts[] = { "SUPER", "ALT", "CTRL", "SHIFT", NULL };
void cycle_mod(int idx) {
	if (idx < 0 || idx >= ncfg) return;
	const char **p = mod_opts;
	while (*p && strcmp(*p, cfields[idx].value)) p++;
	int pos = (int)(p - mod_opts);
	if (!*p) pos = -1;
	pos = (pos + 1) % 4;  /* 4 options, wrap around */
	snprintf(cfields[idx].value, sizeof cfields[idx].value, "%s", mod_opts[pos]);
	dirty = 1;
}

/* =================================================================
 * Loading & saving
 * ================================================================= */

void config_enter(void) {
	/* Free previous allocation (if any) and re-allocate based on
	 * current ncfg so the array is always in sync with cfields[]. */
	if (config_field_apps) {
		for (int i = 0; i < ncfg; i++) {
			free(config_field_apps[i].name);
			free(config_field_apps[i].exec);
			free((void *)config_field_apps[i].desktop);
		}
		free(config_field_apps);
	}
	config_field_apps = calloc(ncfg, sizeof(App));
	config_mode = 1;
	config_edit_idx = 0;
	/* Load current values from swm.conf */
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/swm.conf", cfg_dir());
	FILE *f = fopen(path, "r");
	if (f) {
		char buf[1024];
		int in_s = 0;
		while (fgets(buf, sizeof buf, f)) {
			char *p = trim(buf);
			if (*p == '#' || !*p) continue;
			if (*p == '[') {
				char *end = strchr(p, ']');
				if (!end) continue;
				*end = 0;
				in_s = !strcasecmp(trim(p + 1), "settings");
				continue;
			}
			if (!in_s) continue;
			char *eq = strchr(p, '=');
			if (!eq) continue;
			*eq = 0;
			char *k = trim(p);
			char *v = trim(eq + 1);
			if (!*k || !*v) continue;
			for (int i = 0; i < ncfg; i++) {
				if (!strcmp(k, cfields[i].key)) {
					snprintf(cfields[i].value, sizeof cfields[i].value, "%s", v);
					snprintf(cfields[i].saved, sizeof cfields[i].saved, "%s", cfields[i].value);
					break;
				}
			}
		}
		fclose(f);
	} else {
		for (int i = 0; i < ncfg; i++)
			snprintf(cfields[i].saved, sizeof cfields[i].saved, "%s", cfields[i].value);
	}
	/* Set input to something that won't filter anything,
	 * and prompt the render to show "! swm". */
	snprintf(input, sizeof input, "!swm ");  /* trailing space so no bang matches */
	input_len = (int)strlen(input);
	rebuild();
	dirty = 1;
}

void config_save_all(void) {
	char path[PATH_MAX];
	snprintf(path, sizeof path, "%s/swm.conf", cfg_dir());
	char tmppath[PATH_MAX];
	snprintf(tmppath, sizeof tmppath, "%s/swm.conf.tmp", cfg_dir());

	/* Read existing file, replace [settings] values */
	FILE *f = fopen(path, "r");
	if (!f) { fprintf(stderr, "srun: cannot read %s\n", path); return; }

	size_t cap = 16384, len = 0;
	char *data = malloc(cap);
	if (!data) { fclose(f); return; }
	char buf[1024];
	while (fgets(buf, sizeof buf, f)) {
		size_t bl = strlen(buf);
		if (len + bl + 1 > cap) {
			cap *= 2;
			char *nd = realloc(data, cap);
			if (!nd) { free(data); fclose(f); return; }
			data = nd;
		}
		memcpy(data + len, buf, bl + 1);
		len += bl;
	}
	fclose(f);
	data[len] = 0;

	char *out = malloc(cap + 2048);
	if (!out) { free(data); return; }
	size_t olen = 0;

	int in_s = 0;
	char *line = data;
	while (line && *line) {
		char *nl = strchr(line, '\n');
		if (nl) *nl = 0;

		char *p = trim(line);
		int skip = 0;

		if (*p == '[') {
			char *end = strchr(p, ']');
			if (end) { *end = 0; in_s = !strcasecmp(trim(p + 1), "settings"); }
		} else if (in_s && *p && *p != '#') {
			char *eq = strchr(p, '=');
			if (eq) {
				*eq = 0;
				char *k = trim(p);
				*eq = '=';
				for (int i = 0; i < ncfg; i++) {
					if (!strcmp(k, cfields[i].key)) {
						olen += snprintf(out + olen, cap + 2048 - olen,
							"%s = %s\n", k, cfields[i].value);
						skip = 1;
						break;
					}
				}
			}
		}

		if (!skip) {
			if (nl) *nl = '\n';
			size_t ll = nl ? (size_t)(nl - line + 1) : strlen(line);
			if (olen + ll < cap + 2048) { memcpy(out + olen, line, ll); olen += ll; }
		}
		line = nl ? nl + 1 : NULL;
	}

	/* Write to a temp file first, then rename atomically so a crash
	 * mid-write never corrupts swm.conf.  rename() on the same
	 * filesystem is atomic. */
	f = fopen(tmppath, "w");
	if (f) {
		fwrite(out, 1, olen, f);
		fclose(f);
		if (rename(tmppath, path) == 0) {
			for (int i = 0; i < ncfg; i++)
				snprintf(cfields[i].saved, sizeof cfields[i].saved, "%s", cfields[i].value);
			/* swm watches the config directory with inotify and will
			 * automatically reload settings when swm.conf is closed
			 * after writing — no SIGUSR1 needed. */
		} else {
			fprintf(stderr, "srun: rename %s -> %s failed\n", tmppath, path);
			unlink(tmppath);
		}
	} else {
		fprintf(stderr, "srun: cannot write %s\n", tmppath);
	}
	free(out);
	free(data);
}

/* =================================================================
 * App entries for the filtered list (shared with apps.c / rebuild)
 * ================================================================= */

/* Dynamically allocated App slots that rebuild() fills into filtered[]
 * when config_mode == 1.  Allocated in config_enter() based on ncfg.
 * The label goes into ->name (left column) and the value goes into
 * ->desktop (right column) so the normal two‑column list rendering
 * aligns them cleanly. */
App *config_field_apps = NULL;

/* Find field index by key (used by render to look up type/value). */
int config_find_idx(const char *key) {
	for (int i = 0; i < ncfg; i++)
		if (!strcmp(cfields[i].key, key)) return i;
	return -1;
}

void config_update_field_names(void) {
	for (int i = 0; i < ncfg; i++) {
		/* Name = label only. */
		free(config_field_apps[i].name);
		config_field_apps[i].name = strdup(cfields[i].label);
		/* Exec = field key for lookup. */
		free(config_field_apps[i].exec);
		config_field_apps[i].exec = strdup(cfields[i].key);
		/* Desktop = value (right column). */
		free((void *)config_field_apps[i].desktop);
		if (cfields[i].type == FLD_PATH && strlen(cfields[i].value) > 38) {
			char buf[64];
			snprintf(buf, sizeof buf, "..%s",
				cfields[i].value + strlen(cfields[i].value) - 36);
			config_field_apps[i].desktop = strdup(buf);
		} else {
			config_field_apps[i].desktop = strdup(cfields[i].value);
		}
		config_field_apps[i].icon = NULL;
		config_field_apps[i].icon_surf = NULL;
	}
}

/* Commit the current input buffer as the field value (called from input.c
 * when Enter is pressed in config editing mode). */
void config_commit_edit(void) {
	if (config_edit_idx < 0 || config_edit_idx >= ncfg) return;
	snprintf(cfields[config_edit_idx].value, sizeof cfields[config_edit_idx].value,
		"%s", input);
	config_update_field_names();
	config_save_and_signal();
	config_mode = 1;
	snprintf(input, sizeof input, "!swm ");
	input_len = (int)strlen(input);
	rebuild();
	dirty = 1;
}

/* Return 1 if any field differs from its on‑disk value. */
int config_is_modified(void) {
	for (int i = 0; i < ncfg; i++)
		if (strcmp(cfields[i].value, cfields[i].saved))
			return 1;
	return 0;
}

/* Cancel editing and restore the field's saved value. */
void config_cancel_edit(void) {
	if (config_edit_idx >= 0 && config_edit_idx < ncfg)
		memcpy(cfields[config_edit_idx].value, cfields[config_edit_idx].saved,
			sizeof cfields[config_edit_idx].value);
	config_mode = 1;
	snprintf(input, sizeof input, "!swm ");
	input_len = (int)strlen(input);
	rebuild();
	dirty = 1;
}
