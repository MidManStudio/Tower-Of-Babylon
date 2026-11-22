// ============================================================================
// NOTICE: Full documentation, design decisions, and fix history for this file
// live in docs/floorgen.md, section "floorgen_integer_headless.c"
// ============================================================================
// Keeps the layout code integer only. A floor is built from its seed on the server and on every client, so nothing that decides
// a layout may use float, double or a libm function (fused multiply-add and the trigonometry of each C library are what differ
// between platforms). This test reads the source of the floor generator (src/floorgen/*.h and src/dungeon/dungeon_generator.c) and
// fails on any float or double type, any libm function, math.h, or a float literal, in code (comments are not code). A line may
// carry the marker FLOAT-OK in its comment when a float there is display only and never read by the layout; the generator has
// exactly one. The detector is also tried on lines that must be caught and lines that must not, so it cannot go quiet unnoticed.
// Reads files, so run it from the project root. Inside a macro so mray's build, which compiles every .c file, does not pick up
// a second main().
// Build (one line, from the project root):
//   cc -O2 -std=c99 -DFLOORGEN_INTEGER_HEADLESS_TEST -Isrc tests/floorgen_integer_headless.c -o floorgen_integer_headless
// Run:
//   ./floorgen_integer_headless
#ifdef FLOORGEN_INTEGER_HEADLESS_TEST

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int g_failures = 0;
static void Check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? "pass" : "FAIL", what);
    if (!ok) ++g_failures;
}

static const char *const FORBIDDEN[] = {
    "float", "double",
    "sinf", "cosf", "tanf", "asinf", "acosf", "atanf", "atan2f", "sqrtf", "hypotf", "powf", "expf", "logf", "floorf", "ceilf",
    "roundf", "fabsf", "fmodf", "fmaf",
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sqrt", "hypot", "pow", "exp", "log", "floor", "ceil", "round", "fabs",
    "fmod", "fma", "lround", "lrintf", "rintf", "truncf"
};

// 1 when the code part of the line is not integer only. The comment part ("//" to the end of the line) is not code, and a line
// whose comment says FLOAT-OK is allowed.
static int ScanLine(const char *line, char *found, size_t foundSize)
{
    char code[512];
    size_t n = 0;
    const char *comment = strstr(line, "//");
    if (comment && strstr(comment, "FLOAT-OK")) return 0;
    for (const char *p = line; *p && p != comment && n + 1 < sizeof code; ++p) code[n++] = *p;
    code[n] = 0;
    if (strstr(code, "math.h")) { snprintf(found, foundSize, "math.h"); return 1; }
    for (size_t i = 0; code[i]; ++i) {
        if (isdigit((unsigned char)code[i]) && code[i + 1] == '.' && isdigit((unsigned char)code[i + 2])) { snprintf(found, foundSize, "a float literal"); return 1; }
        if (!(isalpha((unsigned char)code[i]) || code[i] == '_')) continue;
        if (i > 0 && (isalnum((unsigned char)code[i - 1]) || code[i - 1] == '_')) continue;     // not the start of an identifier
        size_t j = i;
        while (isalnum((unsigned char)code[j]) || code[j] == '_') ++j;
        for (size_t k = 0; k < sizeof FORBIDDEN / sizeof FORBIDDEN[0]; ++k)
            if (strlen(FORBIDDEN[k]) == j - i && strncmp(code + i, FORBIDDEN[k], j - i) == 0) { snprintf(found, foundSize, "%s", FORBIDDEN[k]); return 1; }
        i = j - 1;
    }
    return 0;
}

// Scans a file; returns the number of violations (-1 when it cannot be read) and counts the FLOAT-OK lines.
static int ScanFile(const char *path, int *okMarkers)
{
    FILE *f = fopen(path, "r");
    if (!f) { printf("      cannot read %s\n", path); return -1; }
    char line[1024], found[64];
    int bad = 0, lineNo = 0;
    *okMarkers = 0;
    while (fgets(line, sizeof line, f)) {
        ++lineNo;
        const char *c = strstr(line, "//");
        if (c && strstr(c, "FLOAT-OK")) ++*okMarkers;
        if (ScanLine(line, found, sizeof found)) { printf("      %s:%d uses %s\n", path, lineNo, found); ++bad; }
    }
    fclose(f);
    return bad;
}

int main(void)
{
    char why[64];
    printf("The detector\n");
    Check(ScanLine("    float x = 0;", why, sizeof why), "a float variable is caught");
    Check(ScanLine("    double d;", why, sizeof why), "a double is caught");
    Check(ScanLine("    x = sinf(a);", why, sizeof why) && ScanLine("    x = hypotf(a, b);", why, sizeof why) && ScanLine("    n = floor(v);", why, sizeof why), "libm calls are caught");
    Check(ScanLine("#include <math.h>", why, sizeof why), "math.h is caught");
    Check(ScanLine("    t = 2.5;", why, sizeof why) && ScanLine("    t = 0.5f;", why, sizeof why), "float literals are caught");
    Check(!ScanLine("    int floorNumber = FgFloorHash(f);", why, sizeof why), "identifiers that only contain a forbidden word are not caught");
    Check(!ScanLine("    int a = 3; // float and sinf in a comment", why, sizeof why), "a comment is not code");
    Check(!ScanLine("    f->w = (float)n / 9.0f;   // FLOAT-OK: display only", why, sizeof why), "a line marked FLOAT-OK is allowed");
    Check(ScanLine("    f->w = (float)n;   // not marked", why, sizeof why), "the same line without the marker is caught");
    Check(!ScanLine("    h = (h ^ b) * 16777619u;   /* hex 0x1F and 12 are not floats */", why, sizeof why), "integer literals are not caught");

    printf("\nThe layout code\n");
    static const char *const FILES[] = {
        "src/floorgen/fg_fixed.h", "src/floorgen/fg_rng.h", "src/floorgen/fg_clock.h", "src/floorgen/fg_bitrow.h",
        "src/floorgen/fg_probe.h", "src/floorgen/fg_hash.h", "src/dungeon/dungeon_generator.c"
    };
    int allClean = 1, markers = 0, readable = 1;
    for (unsigned i = 0; i < sizeof FILES / sizeof FILES[0]; ++i) {
        int m = 0;
        const int bad = ScanFile(FILES[i], &m);
        if (bad < 0) readable = 0; else if (bad > 0) allClean = 0;
        markers += m;
    }
    Check(readable, "all seven files can be read from the project root");
    Check(allClean, "no float, double, libm function, math.h or float literal in any of them");
    Check(markers == 1, "exactly one FLOAT-OK line in all of them (the display fraction in the generation report)");

    printf("\n%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
#endif
