// theme.c - global colour theme for srun.
//
// srun and ssettings share one global theme file, ~/.config/swm/theme.conf
// (key = #rrggbb or #rrggbbaa). When it is absent, srun falls back to the
// palette below, so its look is unchanged until you add the file.

#include "srun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <limits.h>

Theme theme;

static const char *xdg_config(void) {
	const char *xc = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	static char buf[PATH_MAX];
	if (xc && *xc) snprintf(buf, sizeof buf, "%s", xc);
	else if (home && *home) snprintf(buf, sizeof buf, "%s/.config", home);
	else snprintf(buf, sizeof buf, ".config");
	return buf;
}

static char *trim(char *s) {
	while (*s && isspace((unsigned char)*s)) s++;
	if (!*s) return s;
	char *e = s + strlen(s) - 1;
	while (e > s && isspace((unsigned char)*e)) *e-- = 0;
	return s;
}

static int parse_hex(const char *s, Color *c) {
	if (!s || s[0] != '#') return 0;
	size_t n = strlen(s + 1);
	unsigned int r = 0, g = 0, b = 0, a = 255;
	int got;
	if (n == 6)      got = sscanf(s + 1, "%2x%2x%2x", &r, &g, &b);
	else if (n == 8) got = sscanf(s + 1, "%2x%2x%2x%2x", &r, &g, &b, &a);
	else return 0;
	if (got < 3) return 0;
	c->r = r / 255.0; c->g = g / 255.0; c->b = b / 255.0; c->a = a / 255.0;
	return 1;
}

static void set_color(const char *key, const char *val) {
	Color c;
	if (!parse_hex(val, &c)) return;
	if      (!strcmp(key, "bg"))     theme.bg = c;
	else if (!strcmp(key, "border")) theme.border = c;
	else if (!strcmp(key, "title"))  theme.title = c;
	else if (!strcmp(key, "hint"))   theme.hint = c;
	else if (!strcmp(key, "sep"))    theme.sep = c;
	else if (!strcmp(key, "sel"))    theme.sel = c;
	else if (!strcmp(key, "label"))  theme.label = c;
	else if (!strcmp(key, "text"))   theme.text = c;
	else if (!strcmp(key, "value"))  theme.value = c;
	else if (!strcmp(key, "caret"))  theme.caret = c;
	else if (!strcmp(key, "term"))   theme.term = c;
}

/* Section context */
enum { SECTION_NONE, SECTION_COLORS };

void load_theme(void) {
	theme.bg     = (Color){0.10, 0.10, 0.14, 0.96};
	theme.border = (Color){0.53, 0.71, 0.98, 1.0};
	theme.title  = (Color){0.92, 0.94, 0.98, 1.0};
	theme.hint   = (Color){0.60, 0.62, 0.70, 1.0};
	theme.sep    = (Color){0.35, 0.37, 0.45, 0.70};
	theme.sel    = (Color){0.53, 0.71, 0.98, 0.22};
	theme.label  = (Color){0.98, 0.99, 1.0,  1.0};
	theme.text   = (Color){0.82, 0.84, 0.88, 1.0};
	theme.value  = (Color){0.55, 0.57, 0.65, 1.0};
	theme.caret  = (Color){0.85, 0.90, 1.0,  1.0};
	theme.term   = (Color){1.0,  0.66, 0.36, 1.0};

	char path[PATH_MAX];
	snprintf(path, sizeof path, "%.*s/swm/theme.conf", (int)(sizeof(path) - 16), xdg_config());
	FILE *f = fopen(path, "r");
	if (!f) return;
	char buf[256];
	int section = SECTION_COLORS;
	while (fgets(buf, sizeof buf, f)) {
		char *p = buf;
		while (*p && isspace((unsigned char)*p)) p++;
		if (*p == '#' || *p == 0) continue;
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (!end) continue;
			*end = 0;
			char *name = p + 1;
			while (*name && isspace((unsigned char)*name)) name++;
			if (!strcasecmp(name, "colors"))
				section = SECTION_COLORS;
			continue;
		}
		if (section != SECTION_COLORS) continue;
		char *eq = strchr(p, '=');
		if (!eq) continue;
		*eq = 0;
		set_color(trim(p), trim(eq + 1));
	}
	fclose(f);
}
