// apps.c - application list, .desktop parsing, user config, launching, icons.

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

/* ----- global application list (shared via srun.h) ----- */

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

/* ----- user curation lists (from $XDG_CONFIG_HOME/srun/srun.conf) ----- */
static char **excludes = NULL; static int nexcludes = 0, excl_cap = 0;
static char **includes = NULL; static int nincludes = 0, incl_cap = 0;

static int ci_find(const char *hay, const char *needle); /* defined below */

static void add_pattern(char ***list, int *n, int *cap, const char *s) {
	while (*s == ' ' || *s == '\t') s++;
	size_t L = strlen(s);
	while (L && (s[L-1] == ' ' || s[L-1] == '\t')) L--;
	if (L == 0) return;
	char *d = strndup(s, L);
	if (!d) return;
	if (*n == *cap) { *cap = *cap ? *cap * 2 : 8; *list = realloc(*list, *cap * sizeof(char *)); }
	(*list)[(*n)++] = d;
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

/* ----- config (user curation) ----- */

static void ensure_dir(const char *path) {
	char tmp[1024];
	snprintf(tmp, sizeof tmp, "%s", path);
	char *p = strrchr(tmp, '/');
	if (p) { *p = 0; mkdir(tmp, 0755); }
}

void load_config(void) {
	char buf[1024];
	const char *xc = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xc && *xc) snprintf(buf, sizeof buf, "%s/srun/srun.conf", xc);
	else snprintf(buf, sizeof buf, "%s/.config/srun/srun.conf", home ? home : "");

	FILE *f = fopen(buf, "r");
	if (!f) {                       /* write a default config for the user to edit */
		ensure_dir(buf);
		f = fopen(buf, "w");
		if (f) {
			fprintf(f, "# srun configuration\n");
			fprintf(f, "# Hide or show apps by matching their Name, .desktop filename,\n");
			fprintf(f, "# or executable (case-insensitive substring match).\n");
			fprintf(f, "# exclude = hides a matching app\n");
			fprintf(f, "# include = if any include lines exist, ONLY matching apps are shown\n");
			fprintf(f, "#\n");
			fprintf(f, "# exclude = nm-applet\n");
			fprintf(f, "# exclude = gcr-prompter\n");
			fclose(f);
		}
		return;
	}
	char *line = NULL; size_t n = 0;
	while (getline(&line, &n, f) > 0) {
		line[strcspn(line, "\n")] = 0;
		if (!strncmp(line, "exclude =", 9)) add_pattern(&excludes, &nexcludes, &excl_cap, line + 9);
		else if (!strncmp(line, "include =", 9)) add_pattern(&includes, &nincludes, &incl_cap, line + 9);
	}
	free(line);
	fclose(f);
}

/* ----- filtering ----- */

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

/* ----- launching ----- */

/* Tokenize a .desktop Exec string (quote-aware), dropping % field codes. */
static char **parse_exec(const char *s) {
	char **argv = NULL;
	size_t n = 0, cap = 0;
	const char *p = s;
	while (*p) {
		while (*p && isspace((unsigned char)*p)) p++;
		if (!*p) break;
		char *tok = malloc(strlen(p) + 1);
		size_t ti = 0; int inq = 0;
		while (*p && (inq || !isspace((unsigned char)*p))) {
			if (*p == '"') { inq = !inq; p++; continue; }
			if (*p == '%') { p++; while (*p && !isspace((unsigned char)*p) && *p != '"') p++; continue; }
			tok[ti++] = *p++;
		}
		tok[ti] = 0;
		if (ti) {
			if (n == cap) { cap = cap ? cap * 2 : 8; argv = realloc(argv, cap * sizeof(char *)); }
			argv[n++] = tok;
		}
	}
	if (n == cap) argv = realloc(argv, (n + 1) * sizeof(char *));
	if (argv) argv[n] = NULL;
	return argv;
}

void run_app(App *a) {
	if (!a) return;
	char **argv = parse_exec(a->exec);
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		if (argv && argv[0]) execvp(argv[0], argv);
		execl("/bin/sh", "sh", "-c", a->exec, (char *)NULL);
		_exit(127);
	}
	if (argv) { for (size_t i = 0; argv[i]; i++) free(argv[i]); free(argv); }
	quit = 1;
}

/* ----- terminal detection / spawning ----- */

static int cmd_exists(const char *cmd) {
	if (strchr(cmd, '/')) return access(cmd, X_OK) == 0;
	const char *path = getenv("PATH");
	char *dup = strdup(path ? path : "/usr/local/bin:/usr/bin:/bin");
	if (!dup) return 0;
	char tmp[1024];
	int found = 0;
	for (char *tok = strtok(dup, ":"); tok && !found; tok = strtok(NULL, ":")) {
		snprintf(tmp, sizeof tmp, "%s/%s", tok, cmd);
		if (access(tmp, X_OK) == 0) found = 1;
	}
	free(dup);
	return found;
}

static const char *terminals[] = {
	"st", "alacritty", "kitty", "foot", "urxvt", "rxvt", "xterm",
	"gnome-terminal", "konsole", "terminator", "lxterminal", "mate-terminal",
	NULL
};

/* Pick an available terminal emulator, honoring $TERMINAL first. */
static const char *find_terminal(void) {
	const char *t = getenv("TERMINAL");
	if (t && *t && cmd_exists(t)) return t;
	for (int i = 0; terminals[i]; i++)
		if (cmd_exists(terminals[i])) return terminals[i];
	return NULL;
}

/* Most terminals accept -e; some DE terminals prefer --. */
static const char *term_exec_flag(const char *term) {
	if (!strcmp(term, "gnome-terminal") || !strcmp(term, "terminator") ||
			!strcmp(term, "mate-terminal") || !strcmp(term, "lxterminal") ||
			!strcmp(term, "xfce4-terminal"))
		return "--";
	return "-e";
}

/* The user's login shell, used to run terminal commands. */
static const char *user_shell(void) {
	const char *s = getenv("SHELL");
	return (s && *s) ? s : "/bin/sh";
}

/* Open a new terminal window and run `cmd` inside it (the launcher strips the
 * leading '#' first). The command runs in the user's $SHELL and the terminal
 * stays open afterwards (drops to a shell) so output is visible. An empty
 * command just opens a shell. If no terminal is found we fall back to running
 * the command directly in the user's shell. */
void run_in_terminal(const char *cmd) {
	const char *term = find_terminal();
	const char *shell = user_shell();
	if (!term) {
		pid_t pid = fork();
		if (pid == 0) {
			setsid();
			execl(shell, shell, "-c", cmd ? cmd : "", (char *)NULL);
			_exit(127);
		}
		quit = 1;
		return;
	}
	char *argv[6];
	int n = 0;
	argv[n++] = (char *)term;
	if (cmd && *cmd) {
		size_t need = strlen(cmd) + strlen(shell) + 16;
		char *combined = malloc(need);
		snprintf(combined, need, "%s; exec %s", cmd, shell);
		argv[n++] = (char *)term_exec_flag(term);
		argv[n++] = (char *)shell;
		argv[n++] = "-c";
		argv[n++] = combined;
	}
	argv[n] = NULL;
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execvp(argv[0], argv);
		_exit(127);
	}
	quit = 1;
}

/* ----- icon loading ----- */

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

/* Find a PNG for a theme icon name; returns a malloc'd path or NULL. */
static char *icon_path_for(const char *name) {
	if (!name || !*name) return NULL;
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
