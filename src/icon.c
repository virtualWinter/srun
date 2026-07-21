// icon.c - GTK icon theme resolution and PNG loading for srun.

#include "srun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* Resolve the current GTK icon theme name from settings (fallback: hicolor). */
static int read_gtk_icon_theme(char *out, size_t n) {
	const char *xc = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	char path[2048];
	if (xc && *xc) snprintf(path, sizeof path, "%s/gtk-3.0/settings.ini", xc);
	else snprintf(path, sizeof path, "%s/.config/gtk-3.0/settings.ini", home ? home : "");
	FILE *f = fopen(path, "r");
	if (!f) {
		snprintf(path, sizeof path, "%s/.gtkrc-2.0", home ? home : "");
		f = fopen(path, "r");
		if (!f) return 0;
	}
	char *line = NULL; size_t len = 0; int found = 0;
	while (getline(&line, &len, f) > 0) {
		if (strstr(line, "gtk-icon-theme-name")) {
			char *eq = strchr(line, '=');
			if (eq) {
				eq++;
				while (*eq == ' ' || *eq == '\t' || *eq == '"') eq++;
				size_t L = strlen(eq);
				while (L && (eq[L-1]=='\n'||eq[L-1]=='\r'||eq[L-1]=='"'||eq[L-1]==' ')) eq[--L]=0;
				if (L) { snprintf(out, n, "%.*s", (int)L, eq); found = 1; break; }
			}
		}
	}
	free(line); fclose(f);
	return found;
}

/* Reject icon names that could traverse directories or reach absolute paths. */
static int icon_name_safe(const char *name) {
	if (name[0] == '/') return 0;
	if (strstr(name, "..")) return 0;
	return 1;
}

/* Find a PNG for a theme icon name; returns a malloc'd path or NULL. */
static char *icon_path_for(const char *name) {
	if (!name || !*name || !icon_name_safe(name)) return NULL;
	char theme[128];
	if (!read_gtk_icon_theme(theme, sizeof theme)) strcpy(theme, "hicolor");

	char bases[8][1024]; int nb = 0;
	const char *xdg_data = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	const char *data_dirs = getenv("XDG_DATA_DIRS");
	if (xdg_data && *xdg_data) snprintf(bases[nb++], sizeof bases[0], "%s", xdg_data);
	else if (home) snprintf(bases[nb++], sizeof bases[0], "%s/.local/share", home);

	char dd[2048];
	if (data_dirs && *data_dirs) snprintf(dd, sizeof dd, "%s", data_dirs);
	else snprintf(dd, sizeof dd, "/usr/local/share:/usr/share");
	char *tok = strtok(dd, ":");
	while (tok && nb < 8) { snprintf(bases[nb++], sizeof bases[0], "%s", tok); tok = strtok(NULL, ":"); }

	const char *sizes[] = {"48x48","64x64","32x32","256x256","128x128","96x96","scalable",NULL};
	const char *sub[]   = {"apps","places","categories","devices","mimetypes","status","",NULL};
	const char *themes[] = {theme, "hicolor", NULL};
	char path[10240];
	for (int t = 0; themes[t]; t++)
		for (int b = 0; b < nb; b++)
			for (int s = 0; sizes[s]; s++)
				for (int u = 0; sub[u]; u++) {
					if (sub[u][0])
						snprintf(path, sizeof path, "%s/icons/%s/%s/%s/%s.png",
							bases[b], themes[t], sizes[s], sub[u], name);
					else
						snprintf(path, sizeof path, "%s/icons/%s/%s/%s.png",
							bases[b], themes[t], sizes[s], name);
					if (access(path, R_OK) == 0) return strdup(path);
				}
	/* flat fallbacks */
	for (int b = 0; b < nb; b++) {
		snprintf(path, sizeof path, "%s/pixmaps/%s.png", bases[b], name);
		if (access(path, R_OK) == 0) return strdup(path);
		snprintf(path, sizeof path, "%s/icons/%s.png", bases[b], name);
		if (access(path, R_OK) == 0) return strdup(path);
	}
	return NULL;
}

/* Load an icon into a cairo surface (the caller caches it on the App).
 * PNG files and theme icon names are supported; SVG is skipped (no rsvg
 * dependency). */
cairo_surface_t *load_icon(const char *icon) {
	if (!icon || !*icon) return NULL;
	char *path = NULL;
	if (icon[0] == '/') {
		if (strstr(icon, ".png")) path = strdup(icon);
	} else {
		path = icon_path_for(icon);
	}
	if (!path) return NULL;
	cairo_surface_t *s = cairo_image_surface_create_from_png(path);
	free(path);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(s); return NULL; }
	return s;
}
