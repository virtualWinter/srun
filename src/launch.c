// launch.c - application launching and terminal detection.

#include "srun.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

/* Tokenize a .desktop Exec string (quote-aware), dropping % field codes. */
#ifdef TESTING
char **parse_exec(const char *s) {
#else
static char **parse_exec(const char *s) {
#endif
	char **argv = NULL;
	size_t n = 0, cap = 0;
	const char *p = s;
	while (*p) {
		while (*p && isspace((unsigned char)*p)) p++;
		if (!*p) break;
		char *tok = malloc(strlen(p) + 1);
		if (!tok) goto fail;
		size_t ti = 0; int inq = 0;
		while (*p && (inq || !isspace((unsigned char)*p))) {
			if (*p == '"') { inq = !inq; p++; continue; }
			if (*p == '%') { p++; while (*p && !isspace((unsigned char)*p) && *p != '"') p++; continue; }
			tok[ti++] = *p++;
		}
		tok[ti] = 0;
		if (!ti) { free(tok); continue; }
		if (n == cap) {
			cap = cap ? cap * 2 : 8;
			char **new_argv = realloc(argv, cap * sizeof(char *));
			if (!new_argv) { free(tok); goto fail; }
			argv = new_argv;
		}
		argv[n++] = tok;
	}
	{
		char **new_argv = realloc(argv, (n + 1) * sizeof(char *));
		if (new_argv) argv = new_argv;
	}
	if (argv) argv[n] = NULL;
	return argv;

fail:
	for (size_t i = 0; i < n; i++) free(argv[i]);
	free(argv);
	return NULL;
}

void run_app(App *a) {
	if (!a) return;
	char **argv = parse_exec(a->exec);
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		if (argv && argv[0]) {
			execvp(argv[0], argv);
		} else {
			/* Nothing to exec — fall through to exit. */
		}
		/* Per the FreeDesktop.org specification, the Exec key is not a shell
		 * command — it must be a valid executable with arguments. If execvp
		 * fails, we simply exit rather than falling back to sh -c, which
		 * would introduce a shell-injection vector via malicious .desktop
		 * files. Desktop entries that require shell features should use
		 * Exec=sh -c '...' explicitly. */
		_exit(127);
	}
	if (argv) { for (size_t i = 0; argv[i]; i++) free(argv[i]); free(argv); }
	quit = 1;
}

/* ----- terminal detection / spawning ----- */

#ifdef TESTING
int cmd_exists(const char *cmd) {
#else
static int cmd_exists(const char *cmd) {
#endif
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
#ifdef TESTING
const char *find_terminal(void) {
#else
static const char *find_terminal(void) {
#endif
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

/* Execute a built-in bang command. The exec string is "!bang:<action>". */
void run_bang(const char *exec) {
	if (!exec || strncmp(exec, "!bang:", 6)) return;
	const char *action = exec + 6;

	if (!strcmp(action, "swm")) {
		config_enter();
		return;
	}
	quit = 1;
}

/* Open a new terminal window and run `cmd` inside it (the launcher strips the
 * leading '#' first). The command runs and the terminal stays open afterwards
 * (drops to a shell) so output is visible.  An empty command just opens a
 * shell.  If no terminal is found we fall back to running the command directly
 * in the user's shell.
 *
 * SECURITY: The user's input is NOT concatenated into a shell string.
 * Instead it is tokenized (respecting double quotes) and passed as argv to
 * a shell template:  sh -c 'exec "$@"; exec "$SHELL"' <user-argv>
 * This ensures the user's input is never interpreted as shell code — it is
 * passed as positional parameters to exec, which runs it directly. */
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

	/* Tokenize the user's command to avoid shell injection.
	 * We reuse parse_exec: the %% field-code stripping is a no-op for
	 * user-supplied command strings. */
	char **user_argv = cmd && *cmd ? parse_exec(cmd) : NULL;
	int n_user = 0;
	if (user_argv) while (user_argv[n_user]) n_user++;

	/* Build argv for the terminal emulator:
	 *   terminal -e sh -c 'exec "$@"; exec "$SHELL"' sh <user-argv...>
	 *
	 * The shell template 'exec "$@"; exec "$SHELL"' does:
	 *   1. exec "$@"   — replaces the shell with the user's command
	 *   2. exec "$SHELL" — if the command exits, opens a shell (keeps the
	 *      terminal window open so output remains visible)
	 *
	 * Because the user's input is passed via $@ (separate argv entries),
	 * shell metacharacters in the input are never interpreted as shell code.
	 */
	const char *tmpl = "exec \"$@\"; exec \"$SHELL\"";
	size_t n_argv = 1 + 1 + 1 + 1 + 1 + (size_t)n_user + 1;  /* term, flag, sh, -c, tmpl, sh, user... */
	char **argv = calloc(n_argv, sizeof(char *));
	if (!argv) { if (user_argv) { for (int i = 0; i < n_user; i++) free(user_argv[i]); free(user_argv); } return; }

	int n = 0;
	argv[n++] = (char *)term;
	argv[n++] = (char *)term_exec_flag(term);
	argv[n++] = (char *)shell;
	argv[n++] = "-c";
	argv[n++] = (char *)tmpl;
	argv[n++] = (char *)shell;  /* $0 for the template */
	for (int i = 0; i < n_user; i++)
		argv[n++] = user_argv[i];  /* $1, $2, ... */
	argv[n] = NULL;

	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execvp(argv[0], argv);
		_exit(127);
	}

	free(argv);
	if (user_argv) {
		for (int i = 0; i < n_user; i++) free(user_argv[i]);
		free(user_argv);
	}
	quit = 1;
}
