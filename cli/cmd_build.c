#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

/* Read file into malloc'd buffer */
static char *read_file_contents(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

/* Collect all files from src/ recursively */
#define MAX_FILES 256
static struct { char path[256]; } g_files[MAX_FILES];
static int g_file_count = 0;

static void collect_files(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) && g_file_count < MAX_FILES) {
        if (entry->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { collect_files(path); continue; }
        strncpy(g_files[g_file_count++].path, path, 255);
    }
    closedir(d);
}

int cmd_build(int argc, char **argv) {
    (void)argc; (void)argv;

    if (access("lightshell.json", F_OK) != 0) {
        fprintf(stderr, "Error: lightshell.json not found\n");
        return 1;
    }

    /* Read config */
    char *config = read_file_contents("lightshell.json", NULL);
    if (!config) {
        fprintf(stderr, "Error: cannot read lightshell.json\n");
        return 1;
    }
    printf("[build] Read lightshell.json\n");
    free(config);

    /* Collect source files */
    collect_files("src");
    printf("[build] Found %d source files\n", g_file_count);

    /* Generate embed.c — C file with all source files as byte arrays */
    mkdir("build", 0755);
    FILE *embed = fopen("build/embed.c", "w");
    if (!embed) {
        fprintf(stderr, "Error: cannot create build/embed.c\n");
        return 1;
    }

    fprintf(embed, "/* Auto-generated — embedded app source files */\n");
    fprintf(embed, "#include <stdint.h>\n#include <stddef.h>\n\n");
    fprintf(embed, "typedef struct { const char *name; const uint8_t *data; size_t size; } EmbeddedFile;\n\n");

    for (int i = 0; i < g_file_count; i++) {
        size_t len;
        char *data = read_file_contents(g_files[i].path, &len);
        if (!data) continue;

        /* Sanitize name for C identifier */
        char varname[128];
        snprintf(varname, sizeof(varname), "file_%d", i);

        fprintf(embed, "static const uint8_t %s[] = {", varname);
        for (size_t j = 0; j < len; j++) {
            if (j % 16 == 0) fprintf(embed, "\n    ");
            fprintf(embed, "0x%02x,", (unsigned char)data[j]);
        }
        fprintf(embed, "\n};\n\n");
        free(data);
    }

    fprintf(embed, "const EmbeddedFile embedded_files[] = {\n");
    for (int i = 0; i < g_file_count; i++) {
        fprintf(embed, "    { \"%s\", file_%d, sizeof(file_%d) },\n",
                g_files[i].path, i, i);
    }
    fprintf(embed, "};\n");
    fprintf(embed, "const int embedded_file_count = %d;\n", g_file_count);
    fclose(embed);

    printf("[build] Generated build/embed.c\n");

    /* Compile: clang embed.c + platform layer + libr8e.a */
    /* For now, print the command the user would run */
    printf("[build] To compile, run:\n");
    printf("  clang -O2 build/embed.c \\\n");
    printf("    -framework Metal -framework Cocoa -framework QuartzCore \\\n");
    printf("    -o build/app\n");
    printf("\n[build] (Full compilation will be automated in a future update)\n");

    return 0;
}
