// apps.c - desktop file parsing, application list, and search filtering.

#include "srun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stddef.h>

/* ----- global application list ----- */

App   *apps = NULL;
int    napps = 0, apps_cap = 0;
App  **filtered = NULL;
int    nfiltered = 0;
int    sel = 0, scroll = 0;
char   input[256] = {0};
int    input_len = 0;

/* cached list of executables found in $PATH (used in '#' run-mode) */
static App   *bins = NULL;
static int    nbins = 0, bins_cap = 0, bins_loaded = 0;

/* case-insensitive substring match */
static int ci_find(const char *hay, const char *needle) {
	if (!hay) return 0;
	if (!*needle) return 1;
	for (; *hay; hay++) {
		const char *h = hay, *n = needle;
		while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
			h++; n++;
		}
		if (!*n) return 1;
	}
	return 0;
}

/* case-insensitive substring match against name, .desktop basename, or exec */
static int app_matches(const char *exec, const char *name, const char *desktop, const char *pat) {
	if (ci_find(name, pat)) return 1;
	if (ci_find(desktop, pat)) return 1;
	if (exec) {
		char tok[256]; int j = 0;
		while (exec[j] && exec[j] != ' ' && exec[j] != '\t' && exec[j] != '%' && j < 255)
			tok[j] = exec[j], j++;
		tok[j] = 0;
		if (ci_find(tok, pat)) return 1;
	}
	return 0;
}

void app_add(const char *name, const char *exec, const char *desktop, const char *icon) {
	if (!name || !*name || !exec || !*exec) return;
	/* exclude wins; if an include list exists, only matches are shown */
	if (nexcludes)
		for (int i = 0; i < nexcludes; i++)
			if (app_matches(exec, name, desktop, excludes[i])) return;
	if (nincludes) {
		int ok = 0;
		for (int i = 0; i < nincludes; i++)
			if (app_matches(exec, name, desktop, includes[i])) { ok = 1; break; }
		if (!ok) return;
	}
	if (napps == apps_cap) {
		apps_cap = apps_cap ? apps_cap * 2 : 64;
		apps = realloc(apps, apps_cap * sizeof(App));
	}
	apps[napps].name = strdup(name);
	apps[napps].exec = strdup(exec);
	apps[napps].desktop = desktop ? strdup(desktop) : NULL;
	apps[napps].icon = icon ? strdup(icon) : NULL;
	apps[napps].icon_surf = NULL;
	napps++;
}

static void parse_desktop(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) return;
	char *line = NULL;
	size_t n = 0;
	int in_entry = 0, type_app = 0, nodisplay = 0, hidden = 0, scheme = 0, has_categories = 0;
	char name[256] = {0}, exec[1024] = {0}, icon[1024] = {0};
	const char *base = strrchr(path, '/');
	base = base ? base + 1 : path;

	while (getline(&line, &n, f) > 0) {
		line[strcspn(line, "\n")] = 0;
		if (line[0] == '[') {
			in_entry = !strcmp(line, "[Desktop Entry]");
			continue;
		}
		if (!in_entry) continue;
		if (!strncmp(line, "Type=", 5)) {
			if (!strcmp(line + 5, "Application")) type_app = 1;
		} else if (!strncmp(line, "NoDisplay=", 11) && !strncasecmp(line + 11, "true", 4)) {
			nodisplay = 1;
		} else if (!strncmp(line, "Hidden=", 7) && !strncasecmp(line + 7, "true", 4)) {
			hidden = 1;
		} else if (!strncmp(line, "Categories=", 11)) {
			has_categories = 1;
		} else if (!strncmp(line, "MimeType=", 9) && strstr(line + 9, "x-scheme-handler")) {
			scheme = 1;
		} else if (!strncmp(line, "Name=", 5) && !name[0]) {
			snprintf(name, sizeof name, "%s", line + 5);
		} else if (!strncmp(line, "Exec=", 5) && !exec[0]) {
			snprintf(exec, sizeof exec, "%s", line + 5);
		} else if (!strncmp(line, "Icon=", 5) && !icon[0]) {
			snprintf(icon, sizeof icon, "%s", line + 5);
		}
	}
	free(line);
	fclose(f);
	/* skip anything that isn't a real, launchable application.
	 * A scheme handler (e.g. x-scheme-handler/http) is only dropped when it
	 * has no Categories, so real browsers/apps that also register a handler
	 * are kept. */
	if (hidden || nodisplay || !type_app || !name[0] || !exec[0]
			|| (scheme && !has_categories))
		return;
	app_add(name, exec, base, icon);
}

static void scan_dir(const char *dir) {
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *e;
	char path[1024];
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		if (len > 8 && !strcmp(e->d_name + len - 8, ".desktop")) {
			snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
			parse_desktop(path);
		}
	}
	closedir(d);
}

void load_apps(void) {
	char buf[1024];
	const char *dh = getenv("XDG_DATA_HOME");
	const char *home = getenv("HOME");
	if (dh && *dh)
		snprintf(buf, sizeof buf, "%s/applications", dh);
	else
		snprintf(buf, sizeof buf, "%s/.local/share/applications", home ? home : "");
	scan_dir(buf);
	scan_dir("/usr/share/applications");
	scan_dir("/usr/local/share/applications");

	if (napps == 0) { /* fallback so the launcher is never empty */
		app_add("No apps...",      "echo heh?", NULL, NULL);
	}
}

/* ----- filtering ----- */

static int cmp_app_name(const void *a, const void *b) {
	return strcmp(((const App *)a)->name, ((const App *)b)->name);
}

/* Enumerate every executable in $PATH once and cache them as synthetic apps
 * (name == exec == basename). Used by the '#' run-mode so the user can pick a
 * binary. The first directory in $PATH that contains a name wins (like the
 * shell); duplicates by basename are skipped. */
static void load_bins(void) {
	if (bins_loaded) return;
	bins_loaded = 1;
	const char *path = getenv("PATH");
	char *dup = strdup(path && *path ? path : "/usr/local/bin:/usr/bin:/bin");
	if (!dup) return;
	char full[2048];
	struct stat st;
	for (char *dir = strtok(dup, ":"); dir; dir = strtok(NULL, ":")) {
		DIR *d = opendir(dir);
		if (!d) continue;
		struct dirent *de;
		while ((de = readdir(d))) {
			if (de->d_name[0] == '.') continue;
			snprintf(full, sizeof full, "%s/%s", dir, de->d_name);
			if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
			if (access(full, X_OK) != 0) continue;
			int seen = 0;
			for (int i = 0; i < nbins; i++)
				if (!strcmp(bins[i].name, de->d_name)) { seen = 1; break; }
			if (seen) continue;
			if (nbins == bins_cap) {
				bins_cap = bins_cap ? bins_cap * 2 : 256;
				bins = realloc(bins, bins_cap * sizeof(App));
			}
			App *a = &bins[nbins++];
			a->name = strdup(de->d_name);
			a->exec = strdup(de->d_name);
			a->desktop = NULL; a->icon = NULL; a->icon_surf = NULL;
		}
		closedir(d);
	}
	free(dup);
	qsort(bins, nbins, sizeof(App), cmp_app_name);
}

void rebuild(void) {
	free(filtered);
	filtered = NULL;
	nfiltered = 0;
	sel = 0; scroll = 0;
	dirty = 1;

	/* '#' prefix => browse executables in $PATH (run-in-terminal mode) */
	if (input_len > 0 && input[0] == '#') {
		load_bins();
		const char *q = input + 1;
		int cap = nbins ? nbins : 1;
		filtered = malloc(cap * sizeof(App *));
		for (int i = 0; i < nbins; i++)
			if (ci_find(bins[i].name, q)) filtered[nfiltered++] = &bins[i];
		return;
	}

	if (input_len == 0) {
		filtered = malloc(sizeof(App *) * (napps ? napps : 1));
		for (int i = 0; i < napps; i++) filtered[nfiltered++] = &apps[i];
	} else {
		filtered = malloc(sizeof(App *) * (napps ? napps : 1));
		for (int i = 0; i < napps; i++)
			if (ci_find(apps[i].name, input) || ci_find(apps[i].exec, input))
				filtered[nfiltered++] = &apps[i];
	}
}
