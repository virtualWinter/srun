/* Unit tests for srun.
 *
 * Tests pure-logic functions from theme.c, launch.c, apps.c, and
 * config_sub.c.  These source files are compiled with -DTESTING so
 * that internal functions are exposed.  The test binary links only
 * -lm (no Wayland, Cairo, or xkbcommon) because the tested functions
 * are pure C string/numeric logic.
 *
 * Build:
 *   cc -DTESTING $(CFLAGS) -c src/theme.c      -o build/test_theme.o
 *   cc -DTESTING $(CFLAGS) -c src/launch.c     -o build/test_launch.o
 *   cc -DTESTING $(CFLAGS) -c src/apps.c       -o build/test_apps.o
 *   cc -DTESTING $(CFLAGS) -c src/config_sub.c -o build/test_config_sub.o
 *   cc -DTESTING $(CFLAGS) -I src -c test/tests.c  -o build/test_tests.o
 *   cc build/test_*.o -o build/test_srun -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>

/* ----- srun types we need ----- */

/* Pull in App, Color, Theme, and the test-exposed function prototypes.
 * srun.h includes wayland-client.h, cairo.h, and xkbcommon.h — those
 * headers provide type declarations only; no symbols from those libraries
 * are referenced, so no link-time dependency is introduced. */
#include "srun.h"

/* =================================================================
 * Test framework helpers
 * ================================================================= */

static int total  = 0;
static int passed = 0;

#define ASSERT(cond, fmt, ...) do {                                    \
	total++;                                                           \
	if (!(cond)) {                                                     \
		fprintf(stderr, "  FAIL [%d] " fmt "\n", total, ##__VA_ARGS__);\
	} else {                                                           \
		passed++;                                                      \
	}                                                                  \
} while (0)

#define TEST(name) do {                                  \
	printf("  " name "...\n");                           \
} while (0)

static int f_near(float a, float b) {
	return fabsf(a - b) < 0.001f;
}

/* =================================================================
 * Theme — colour parsing
 * ================================================================= */

/* Exposed by theme.c under TESTING. */
int parse_hex(const char *s, Color *c);

static void test_theme_defaults(void) {
	TEST("theme defaults before load_theme");
	/* load_theme() hasn't been called, but Theme is a global with
	 * static storage, so everything is zero.  This just confirms
	 * the struct layout. */
	Theme t = {0};
	(void)t;
}

static void test_parse_hex_rrggbb(void) {
	TEST("parse_hex #rrggbb");
	Color c;

	ASSERT(parse_hex("#ff0000", &c), "parse red");
	ASSERT(f_near(c.r, 1.0f), "red r=1 got %g", c.r);
	ASSERT(f_near(c.g, 0.0f), "red g=0 got %g", c.g);
	ASSERT(f_near(c.b, 0.0f), "red b=0 got %g", c.b);
	ASSERT(f_near(c.a, 1.0f), "red a=1 got %g", c.a);

	ASSERT(parse_hex("#00ff00", &c), "parse green");
	ASSERT(f_near(c.r, 0.0f) && f_near(c.g, 1.0f) && f_near(c.b, 0.0f),
		"green = (%g,%g,%g)", c.r, c.g, c.b);

	ASSERT(parse_hex("#0000ff", &c), "parse blue");
	ASSERT(f_near(c.r, 0.0f) && f_near(c.g, 0.0f) && f_near(c.b, 1.0f),
		"blue = (%g,%g,%g)", c.r, c.g, c.b);

	ASSERT(parse_hex("#ffffff", &c), "parse white");
	ASSERT(f_near(c.r, 1.0f) && f_near(c.g, 1.0f) && f_near(c.b, 1.0f),
		"white = (%g,%g,%g)", c.r, c.g, c.b);

	ASSERT(parse_hex("#000000", &c), "parse black");
	ASSERT(f_near(c.r, 0.0f) && f_near(c.g, 0.0f) && f_near(c.b, 0.0f),
		"black = (%g,%g,%g)", c.r, c.g, c.b);

	/* swm defaults */
	ASSERT(parse_hex("#333333", &c), "parse #333333");
	ASSERT(f_near(c.r, 0.2f), "#333333 r=0.2 got %g", c.r);

	ASSERT(parse_hex("#c831dc", &c), "parse #c831dc");
	ASSERT(f_near(c.r, 0.784f), "r=0.784 got %g", c.r);
	ASSERT(f_near(c.g, 0.192f), "g=0.192 got %g", c.g);
	ASSERT(f_near(c.b, 0.863f), "b=0.863 got %g", c.b);
}

static void test_parse_hex_rrggbbaa(void) {
	TEST("parse_hex #rrggbbaa");
	Color c;

	ASSERT(parse_hex("#ff000080", &c), "parse red at half alpha");
	ASSERT(f_near(c.r, 1.0f), "r=1 got %g", c.r);
	ASSERT(f_near(c.a, 0.502f), "a=0.502 got %g", c.a);

	ASSERT(parse_hex("#00000000", &c), "parse fully transparent");
	ASSERT(f_near(c.a, 0.0f), "a=0 got %g", c.a);

	ASSERT(parse_hex("#ffffffff", &c), "parse fully opaque");
	ASSERT(f_near(c.a, 1.0f), "a=1 got %g", c.a);
}

static void test_parse_hex_invalid(void) {
	TEST("parse_hex invalid inputs");
	Color c;

	ASSERT(!parse_hex(NULL, &c),      "NULL string");
	ASSERT(!parse_hex("", &c),        "empty string");
	ASSERT(!parse_hex("#", &c),       "just hash");
	ASSERT(!parse_hex("#ff", &c),     "too short");
	ASSERT(!parse_hex("#fffff", &c),  "5 hex digits");
	ASSERT(!parse_hex("#gggggg", &c), "invalid hex chars");
	ASSERT(!parse_hex("ff0000", &c),  "missing hash");
	ASSERT(!parse_hex("#fffffff", &c),"7 hex digits (wrong length)");
	ASSERT(!parse_hex("#fffffffff", &c),"9 hex digits (wrong length)");
}

/* =================================================================
 * Config sub — field model
 * ================================================================= */

static void test_config_sub_field_model(void) {
	TEST("config_sub field model");
	int nf = config_nfields();

	/* Must match the cfields array in config_sub.c */
	ASSERT(nf == 6, "6 fields, got %d", nf);

	/* Every field should have a non-empty label, key, and value */
	for (int i = 0; i < nf; i++) {
		const char *label = config_field_label(i);
		const char *key   = config_field_key(i);
		const char *value = config_field_value(i);
		ASSERT(label && label[0], "field %d has label", i);
		ASSERT(key && key[0],     "field %d has key", i);
		ASSERT(value != NULL,     "field %d has non-NULL value", i);
	}

	/* Known keys */
	ASSERT(config_find_idx("mod") >= 0,          "mod found");
	ASSERT(config_find_idx("border_width") >= 0,  "border_width found");
	ASSERT(config_find_idx("gap") >= 0,           "gap found");
	ASSERT(config_find_idx("border_normal") >= 0, "border_normal found");
	ASSERT(config_find_idx("border_select") >= 0, "border_select found");
	ASSERT(config_find_idx("wallpaper") >= 0,     "wallpaper found");

	/* Unknown key returns -1 */
	ASSERT(config_find_idx("nonexistent") < 0, "unknown key -> -1");

	/* config_is_modified compares value vs. saved.  The static cfields[]
	 * defaults (e.g. "SUPER", "1") differ from the zero-initialised saved
	 * fields (""), so the config is reported as modified until
	 * config_enter() loads the real file and syncs both.  That's correct:
	 * it means 'different from what is on disk right now'. */
	ASSERT(config_is_modified(), "modified vs zero-initialised saved state");
}

static void test_config_sub_types(void) {
	TEST("config_sub field types");
	int nf = config_nfields();
	for (int i = 0; i < nf; i++) {
		int t = config_field_type(i);
		ASSERT(t >= 0, "field %d has valid type %d", i, t);
		/* We just verify it's one of the expected values */
		ASSERT(t >= 0 && t <= 4, "field %d type in 0-4, got %d", i, t);
	}
}

/* =================================================================
 * Config sub — colour-picker helpers
 * ================================================================= */

static void test_parse_hex_color(void) {
	TEST("parse_hex_color");
	int r, g, b;

	ASSERT(parse_hex_color("#ff0000", &r, &g, &b), "parse red");
	ASSERT(r == 255 && g == 0 && b == 0, "red=(255,0,0) got (%d,%d,%d)", r, g, b);

	ASSERT(parse_hex_color("#00ff00", &r, &g, &b), "parse green");
	ASSERT(r == 0 && g == 255 && b == 0, "green=(0,255,0)");

	ASSERT(parse_hex_color("#0000ff", &r, &g, &b), "parse blue");
	ASSERT(r == 0 && g == 0 && b == 255, "blue=(0,0,255)");

	ASSERT(parse_hex_color("#000000", &r, &g, &b), "parse black");
	ASSERT(r == 0 && g == 0 && b == 0, "black=(0,0,0)");

	ASSERT(parse_hex_color("#ffffff", &r, &g, &b), "parse white");
	ASSERT(r == 255 && g == 255 && b == 255, "white=(255,255,255)");

	/* Invalid */
	ASSERT(!parse_hex_color(NULL, &r, &g, &b),       "NULL");
	ASSERT(!parse_hex_color("#ff", &r, &g, &b),       "too short");
	ASSERT(!parse_hex_color("#gggggg", &r, &g, &b),   "invalid hex");
	ASSERT(!parse_hex_color("ff0000", &r, &g, &b),    "no hash");
}

static void test_hsv_roundtrip(void) {
	TEST("RGB↔HSV round-trip");
	int test_colors[][3] = {
		{255, 0, 0},     /* red */
		{0, 255, 0},     /* green */
		{0, 0, 255},     /* blue */
		{0, 0, 0},       /* black */
		{255, 255, 255}, /* white */
		{128, 128, 128}, /* grey */
		{255, 128, 0},   /* orange */
		{128, 0, 255},   /* purple */
	};
	int nt = sizeof(test_colors) / sizeof(test_colors[0]);

	for (int i = 0; i < nt; i++) {
		int r = test_colors[i][0];
		int g = test_colors[i][1];
		int b = test_colors[i][2];
		float h, s, v;

		rgb_to_hsv(r, g, b, &h, &s, &v);
		/* Convert back */
		hsv_to_rgb(h, s, v);

		/* We use the globals cp_r/cp_g/cp_b set by hsv_to_rgb */
		extern int cp_r, cp_g, cp_b;
		ASSERT(abs(cp_r - r) <= 1 && abs(cp_g - g) <= 1 && abs(cp_b - b) <= 1,
			"color %d: (%d,%d,%d) → HSV → (%d,%d,%d)",
			i, r, g, b, cp_r, cp_g, cp_b);
	}
}

static void test_hsv_known(void) {
	TEST("HSV known values");
	extern int cp_r, cp_g, cp_b;

	/* Red: h=0, s=1, v=1 */
	hsv_to_rgb(0.0f, 1.0f, 1.0f);
	ASSERT(cp_r == 255 && cp_g == 0 && cp_b == 0,
		"red (255,0,0) got (%d,%d,%d)", cp_r, cp_g, cp_b);

	/* Green: h=120, s=1, v=1 */
	hsv_to_rgb(120.0f, 1.0f, 1.0f);
	ASSERT(cp_r == 0 && cp_g == 255 && cp_b == 0,
		"green (0,255,0) got (%d,%d,%d)", cp_r, cp_g, cp_b);

	/* Blue: h=240, s=1, v=1 */
	hsv_to_rgb(240.0f, 1.0f, 1.0f);
	ASSERT(cp_r == 0 && cp_g == 0 && cp_b == 255,
		"blue (0,0,255) got (%d,%d,%d)", cp_r, cp_g, cp_b);

	/* Black: v=0 */
	hsv_to_rgb(0.0f, 0.0f, 0.0f);
	ASSERT(cp_r == 0 && cp_g == 0 && cp_b == 0,
		"black (0,0,0) got (%d,%d,%d)", cp_r, cp_g, cp_b);

	/* White: s=0, v=1 */
	hsv_to_rgb(0.0f, 0.0f, 1.0f);
	ASSERT(cp_r == 255 && cp_g == 255 && cp_b == 255,
		"white (255,255,255) got (%d,%d,%d)", cp_r, cp_g, cp_b);
}

/* =================================================================
 * Launch — exec parsing
 * ================================================================= */

/* Exposed by launch.c under TESTING. */
char **parse_exec(const char *s);
int  cmd_exists(const char *cmd);
const char *find_terminal(void);

/* Exposed by theme.c under TESTING. */
void set_value(const char *key, const char *val);

/* Exposed by config.c under TESTING. */
void add_pattern(char ***list, int *n, int *cap, const char *s);
void test_reset_config(void);

static void test_parse_exec_simple(void) {
	TEST("parse_exec simple");
	char **argv = parse_exec("foot");
	ASSERT(argv != NULL, "got argv");
	ASSERT(argv[0] != NULL && !strcmp(argv[0], "foot"), "argv[0]=foot");
	ASSERT(argv[1] == NULL, "exactly one arg");
	free(argv[0]);
	free(argv);
}

static void test_parse_exec_args(void) {
	TEST("parse_exec with arguments");
	char **argv = parse_exec("foot --server /tmp/foot.sock");
	ASSERT(argv != NULL, "got argv");
	ASSERT(argv[0] && !strcmp(argv[0], "foot"),          "argv[0]=foot");
	ASSERT(argv[1] && !strcmp(argv[1], "--server"),      "argv[1]=--server");
	ASSERT(argv[2] && !strcmp(argv[2], "/tmp/foot.sock"), "argv[2]=path");
	ASSERT(argv[3] == NULL, "exactly 3 args");
	for (int i = 0; argv[i]; i++) free(argv[i]);
	free(argv);
}

static void test_parse_exec_quotes(void) {
	TEST("parse_exec with quoted string");
	char **argv = parse_exec("foot \"hello world\"");
	ASSERT(argv != NULL, "got argv");
	ASSERT(argv[0] && !strcmp(argv[0], "foot"),          "argv[0]=foot");
	ASSERT(argv[1] && !strcmp(argv[1], "hello world"),   "argv[1]=hello world");
	ASSERT(argv[2] == NULL, "exactly 2 args");
	for (int i = 0; argv[i]; i++) free(argv[i]);
	free(argv);
}

static void test_parse_exec_field_codes(void) {
	TEST("parse_exec strips %% field codes");
	/* %f, %F, %u, %U, %i, %c, %k should be dropped */
	char **argv = parse_exec("foot %f %F %u %U %i %c %k");
	ASSERT(argv != NULL, "got argv");
	ASSERT(argv[0] && !strcmp(argv[0], "foot"), "argv[0]=foot");
	/* All % codes should be dropped — argv[1] should be NULL */
	ASSERT(argv[1] == NULL, "all field codes stripped");
	free(argv[0]);
	free(argv);
}

static void test_parse_exec_empty(void) {
	TEST("parse_exec empty string");
	char **argv = parse_exec("");
	/* Should return NULL or {NULL} */
	ASSERT(argv == NULL || argv[0] == NULL, "empty -> NULL or NULL-terminated");
	if (argv) free(argv);
}

static void test_cmd_exists(void) {
	TEST("cmd_exists");
	/* /bin/sh should always exist on a POSIX system */
	ASSERT(cmd_exists("/bin/sh") != 0, "/bin/sh exists");
	ASSERT(cmd_exists("/nonexistent_dead_beef") == 0, "nonexistent returns 0");
	/* sh should be found via PATH */
	ASSERT(cmd_exists("sh") != 0, "sh found in PATH");
	ASSERT(cmd_exists("thiscommanddoesnotexist99999") == 0, "random command not found");
}

/* =================================================================
 * Apps — filtering
 * ================================================================= */

/* Exposed by apps.c under TESTING. */
int ci_find(const char *hay, const char *needle);
int app_matches(const char *exec, const char *name, const char *desktop, const char *pat);

static void test_ci_find(void) {
	TEST("ci_find case-insensitive substring");

	/* Exact matches */
	ASSERT(ci_find("Firefox", "firefox"), "Firefox contains firefox");
	ASSERT(ci_find("firefox", "FIREFOX"), "firefox contains FIREFOX");
	ASSERT(ci_find("FiReFoX", "firefox"), "FiReFoX contains firefox");

	/* Substrings */
	ASSERT(ci_find("Mozilla Firefox", "fox"), "Mozilla Firefox contains fox");
	ASSERT(ci_find("Alacritty", "crit"),     "Alacritty contains crit");

	/* No match */
	ASSERT(!ci_find("Firefox", "chrome"), "Firefox !~ chrome");
	ASSERT(!ci_find("", "anything"),      "empty hay -> 0");

	/* Empty needle should match anything */
	ASSERT(ci_find("anything", ""), "empty needle -> 1");

	/* NULL hay */
	ASSERT(!ci_find(NULL, "test"), "NULL hay -> 0");
}

/* =================================================================
 * Theme — set_value
 * ================================================================= */

/* Save and restore theme around tests that modify it. */
static Theme saved_theme;
static void save_theme(void) { saved_theme = theme; }
static void restore_theme(void) { theme = saved_theme; }

static void test_set_value_colors(void) {
	TEST("set_value colours");
	save_theme();

	set_value("bg", "#ff0000");
	ASSERT(f_near(theme.bg.r, 1.0f) && f_near(theme.bg.g, 0.0f) && f_near(theme.bg.b, 0.0f),
		"bg set to red");

	set_value("border", "#00ff00");
	ASSERT(f_near(theme.border.g, 1.0f), "border green");

	set_value("title", "#0000ff");
	ASSERT(f_near(theme.title.b, 1.0f), "title blue");

	set_value("sel", "#ffffff");
	ASSERT(f_near(theme.sel.r, 1.0f), "sel white");

	set_value("radius", "8");
	ASSERT(theme.radius == 8, "radius=8, got %d", theme.radius);

	set_value("radius", "-1");
	ASSERT(theme.radius == 8, "negative radius ignored, still %d", theme.radius);

	/* Unknown key should be silently ignored */
	int old_radius = theme.radius;
	set_value("nonexistent", "#ff0000");
	ASSERT(theme.radius == old_radius, "unknown key leaves state unchanged");

	restore_theme();
}

/* =================================================================
 * Config sub — cycle_mod
 * ================================================================= */

static void test_cycle_mod(void) {
	TEST("cycle_mod");

	/* mod is field index 0 */
	int mod_idx = config_find_idx("mod");
	ASSERT(mod_idx == 0, "mod is field 0");

	/* Save original value */
	char orig[256];
	snprintf(orig, sizeof orig, "%s", config_field_value(mod_idx));

	cycle_mod(mod_idx);
	const char *v1 = config_field_value(mod_idx);
	ASSERT(strcmp(v1, orig) != 0, "mod value changed after cycle, was %s now %s", orig, v1);

	/* Cycle 3 more times (4 total) — should return to original */
	cycle_mod(mod_idx);
	cycle_mod(mod_idx);
	cycle_mod(mod_idx);
	const char *v4 = config_field_value(mod_idx);
	ASSERT(strcmp(v4, orig) == 0, "after 4 cycles, back to '%s', got '%s'", orig, v4);

	/* Out-of-range index is a no-op */
	int orig_type = config_field_type(-1);
	cycle_mod(-1);
	ASSERT(config_field_type(-1) == orig_type, "out-of-range no-op");
}

/* =================================================================
 * Config.c — pattern list builder
 * ================================================================= */

static void test_add_pattern(void) {
	TEST("add_pattern");
	test_reset_config();

	char **list = NULL;
	int n = 0, cap = 0;

	add_pattern(&list, &n, &cap, "firefox");
	ASSERT(n == 1, "1 pattern added, got %d", n);
	ASSERT(list && !strcmp(list[0], "firefox"), "pattern value");

	add_pattern(&list, &n, &cap, "  gcr-prompter  ");
	ASSERT(n == 2, "2 patterns, got %d", n);
	ASSERT(list && !strcmp(list[1], "gcr-prompter"), "trimmed whitespace");

	add_pattern(&list, &n, &cap, "");
	ASSERT(n == 2, "empty pattern not added, still %d", n);

	add_pattern(&list, &n, &cap, "   ");
	ASSERT(n == 2, "whitespace-only not added, still %d", n);

	for (int i = 0; i < n; i++) free(list[i]);
	free(list);
}

/* =================================================================
 * Config.c — load_config with a temp file
 * ================================================================= */

static void test_load_config_temp(void) {
	TEST("load_config with temp file");
	test_reset_config();

	/* Create a temp config directory (load_config looks in XDG_CONFIG_HOME/srun/) */
	const char *tmpdir = getenv("TMPDIR");
	if (!tmpdir) tmpdir = "/tmp";
	char dir[256];
	snprintf(dir, sizeof dir, "%s/srun-test-%d", tmpdir, (rand() % 100000));
	char sub[512];
	snprintf(sub, sizeof sub, "%s/srun", dir);
	mkdir(dir, 0755);
	mkdir(sub, 0755);

	/* Write a test config */
	char path[1024];
	snprintf(path, sizeof path, "%s/srun.conf", sub);
	FILE *f = fopen(path, "w");
	ASSERT(f != NULL, "created temp config file");
	if (f) {
		fprintf(f, "[filter]\n");
		fprintf(f, "exclude = nm-applet\n");
		fprintf(f, "exclude = gcr-prompter\n");
		fprintf(f, "include = firefox\n");
		fclose(f);
	}

	/* Override XDG_CONFIG_HOME and trigger load */
	char old_xdg[512] = "";
	const char *orig = getenv("XDG_CONFIG_HOME");
	if (orig) snprintf(old_xdg, sizeof old_xdg, "%s", orig);
	setenv("XDG_CONFIG_HOME", dir, 1);

	test_reset_config();
	load_config();

	/* Verify the patterns were loaded */
	ASSERT(nexcludes == 2, "2 excludes, got %d", nexcludes);
	ASSERT(excludes && !strcmp(excludes[0], "nm-applet"), "first exclude");
	ASSERT(excludes && !strcmp(excludes[1], "gcr-prompter"), "second exclude");
	ASSERT(nincludes == 1, "1 include, got %d", nincludes);
	ASSERT(includes && !strcmp(includes[0], "firefox"), "include");

	/* Restore XDG_CONFIG_HOME */
	if (orig) setenv("XDG_CONFIG_HOME", old_xdg, 1);
	else unsetenv("XDG_CONFIG_HOME");

	/* Clean up */
	unlink(path);
	rmdir(sub);
	rmdir(dir);
	test_reset_config();
}

/* =================================================================
 * Theme — set_value
 * ================================================================= */

static void test_app_matches(void) {
	TEST("app_matches");

	/* Match by name */
	ASSERT(app_matches("firefox", "Firefox Web Browser", "firefox.desktop", "firefox"),
		"name match");
	ASSERT(app_matches("firefox", "Firefox Web Browser", "firefox.desktop", "browser"),
		"partial name match");

	/* Match by exec */
	ASSERT(app_matches("firefox", "Firefox", "firefox.desktop", "fox"),
		"exec token match");

	/* Match by desktop */
	ASSERT(app_matches("firefox", "Firefox", "firefox.desktop", "desktop"),
		"desktop match");

	/* No match */
	ASSERT(!app_matches("firefox", "Firefox", "firefox.desktop", "chrome"),
		"no false match");

	/* NULL fields */
	ASSERT(!app_matches("firefox", NULL, NULL, "chrome"), "NULL fields no match");
	ASSERT(app_matches("firefox", NULL, NULL, "firefox"), "match on exec with NULL fields");
}

/* =================================================================
 * Main
 * ================================================================= */
int main(void) {
	printf("srun test suite\n"
	       "===============\n\n");

	printf("Theme:\n");
	test_theme_defaults();
	test_parse_hex_rrggbb();
	test_parse_hex_rrggbbaa();
	test_parse_hex_invalid();

	printf("\nConfig sub (field model):\n");
	test_config_sub_field_model();
	test_config_sub_types();

	printf("\nConfig sub (colour picker):\n");
	test_parse_hex_color();
	test_hsv_roundtrip();
	test_hsv_known();

	printf("\nLaunch:\n");
	test_parse_exec_simple();
	test_parse_exec_args();
	test_parse_exec_quotes();
	test_parse_exec_field_codes();
	test_parse_exec_empty();
	test_cmd_exists();

	printf("\nTheme set_value:\n");
	test_set_value_colors();

	printf("\nConfig sub (cycle_mod):\n");
	test_cycle_mod();

	printf("\nConfig pattern builder:\n");
	test_add_pattern();

	printf("\nConfig loader (temp file):\n");
	test_load_config_temp();

	printf("\nApps:\n");
	test_ci_find();
	test_app_matches();

	printf("\nResults: %d / %d passed\n", passed, total);
	return total == passed ? 0 : 1;
}
