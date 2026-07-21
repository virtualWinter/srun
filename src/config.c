// config.c - srun.user curation config loader.
//
// Reads an XDG-resolved config file ($XDG_CONFIG_HOME/srun/srun.conf,
// falling back to $HOME/.config/srun/srun.conf). If the file does not
// exist, a default is written so the user has an editable template.
//
// Config syntax:
//
//   [filter]
//   exclude = <pattern>
//   include = <pattern>

#include "srun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>

/* ----- user curation lists ----- */
char **excludes = NULL; int nexcludes = 0, excl_cap = 0;
char **includes = NULL; int nincludes = 0, incl_cap = 0;

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

static void ensure_dir(const char *path) {
	char tmp[1024];
	snprintf(tmp, sizeof tmp, "%s", path);
	char *p = strrchr(tmp, '/');
	if (p) { *p = 0; mkdir(tmp, 0755); }
}

/* Config sections */
enum { SECTION_NONE, SECTION_FILTER };

void load_config(void) {
	char buf[1024];
	const char *xc = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xc && *xc) snprintf(buf, sizeof buf, "%s/srun/srun.conf", xc);
	else snprintf(buf, sizeof buf, "%s/.config/srun/srun.conf", home ? home : "");

	FILE *f = fopen(buf, "r");
	if (!f) {
		ensure_dir(buf);
		f = fopen(buf, "w");
		if (f) {
			fprintf(f, "# srun configuration\n");
			fprintf(f, "# Hide or show apps by matching their Name, .desktop filename,\n");
			fprintf(f, "# or executable (case-insensitive substring match).\n");
			fprintf(f, "#\n");
			fprintf(f, "[filter]\n");
			fprintf(f, "\n");
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
	int section = SECTION_FILTER;
	while (getline(&line, &n, f) > 0) {
		line[strcspn(line, "\n")] = 0;
		char *p = line;
		while (*p && isspace((unsigned char)*p)) p++;
		if (*p == '#' || *p == 0) continue;
		if (*p == '[') {
			char *end = strchr(p, ']');
			if (!end) continue;
			*end = 0;
			char *name = p + 1;
			while (*name && isspace((unsigned char)*name)) name++;
			if (!strcasecmp(name, "filter"))
				section = SECTION_FILTER;
			continue;
		}
		if (section == SECTION_FILTER) {
			if (!strncmp(p, "exclude =", 9)) add_pattern(&excludes, &nexcludes, &excl_cap, p + 9);
			else if (!strncmp(p, "include =", 9)) add_pattern(&includes, &nincludes, &incl_cap, p + 9);
		}
	}
	free(line);
	fclose(f);
}
