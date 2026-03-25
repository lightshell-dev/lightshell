#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>

/* Simple file watcher: poll mtimes every 500ms */
#define MAX_WATCHED 256
static struct { char path[256]; time_t mtime; } g_watched[MAX_WATCHED];
static int g_watch_count = 0;

static time_t get_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

static void scan_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) && g_watch_count < MAX_WATCHED) {
        if (entry->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) { scan_dir(path); continue; }
        /* Watch .js, .html, .css files */
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && (strcmp(ext, ".js") == 0 || strcmp(ext, ".html") == 0 ||
                    strcmp(ext, ".css") == 0 || strcmp(ext, ".json") == 0)) {
            strncpy(g_watched[g_watch_count].path, path, 255);
            g_watched[g_watch_count].mtime = st.st_mtime;
            g_watch_count++;
        }
    }
    closedir(d);
}

static bool check_changes(void) {
    for (int i = 0; i < g_watch_count; i++) {
        time_t now = get_mtime(g_watched[i].path);
        if (now != g_watched[i].mtime) {
            printf("[dev] Changed: %s\n", g_watched[i].path);
            g_watched[i].mtime = now;
            return true;
        }
    }
    return false;
}

int cmd_dev(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Check for lightshell.json */
    if (access("lightshell.json", F_OK) != 0) {
        fprintf(stderr, "Error: lightshell.json not found. Run 'lightshell init' first.\n");
        return 1;
    }

    printf("[dev] Starting development server...\n");
    printf("[dev] Watching src/ for changes\n");

    /* Initial file scan */
    scan_dir("src");
    printf("[dev] Watching %d files\n", g_watch_count);

    /* TODO: Launch the app window with r8e + Metal rendering.
     * For now, just watch for file changes. In the full implementation,
     * this would create a platform window, load the entry HTML,
     * and reload on file changes. */

    printf("[dev] App running at window (press Ctrl+C to stop)\n\n");

    /* Poll for changes */
    while (1) {
        if (check_changes()) {
            printf("[dev] Reloading...\n");
            /* In full implementation: destroy r8e context, recreate, re-eval */
        }
        usleep(500000); /* 500ms */
    }

    return 0;
}
