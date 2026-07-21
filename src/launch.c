// launch.c - application launching and terminal detection.

#include "srun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

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
