#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

/* Forward declaration — defined in cmd_build.c */
int cmd_build(int argc, char **argv);

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
    bool changed = false;
    for (int i = 0; i < g_watch_count; i++) {
        time_t now = get_mtime(g_watched[i].path);
        if (now != g_watched[i].mtime) {
            printf("[dev] Changed: %s\n", g_watched[i].path);
            g_watched[i].mtime = now;
            changed = true;
        }
    }
    return changed;
}

static volatile pid_t g_app_pid = 0;

static void kill_app(void) {
    if (g_app_pid > 0) {
        kill(g_app_pid, SIGTERM);
        int status;
        waitpid(g_app_pid, &status, 0);
        g_app_pid = 0;
    }
}

static void sigint_handler(int sig) {
    (void)sig;
    kill_app();
    printf("\n[dev] Stopped.\n");
    _exit(0);
}

static pid_t launch_app(void) {
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: exec the app */
        execl("./build/app", "./build/app", (char *)NULL);
        perror("exec build/app");
        _exit(1);
    }
    return pid;
}

int cmd_dev(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Check for lightshell.json */
    if (access("lightshell.json", F_OK) != 0) {
        fprintf(stderr, "Error: lightshell.json not found. Run 'lightshell init' first.\n");
        return 1;
    }

    signal(SIGINT, sigint_handler);

    printf("[dev] Starting development server...\n");

    /* Build the app first */
    int ret = cmd_build(0, NULL);
    if (ret != 0) {
        fprintf(stderr, "[dev] Build failed\n");
        return ret;
    }

    /* Launch the app */
    printf("[dev] Launching app...\n");
    g_app_pid = launch_app();
    if (g_app_pid < 0) {
        fprintf(stderr, "[dev] Failed to launch app\n");
        return 1;
    }

    /* Watch for file changes */
    scan_dir("src");
    printf("[dev] Watching %d files (press Ctrl+C to stop)\n\n", g_watch_count);

    while (1) {
        /* Check if app exited on its own */
        if (g_app_pid > 0) {
            int status;
            pid_t p = waitpid(g_app_pid, &status, WNOHANG);
            if (p > 0) {
                g_app_pid = 0;
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    printf("[dev] App exited normally.\n");
                    return 0;
                }
                printf("[dev] App exited with code %d\n",
                       WIFEXITED(status) ? WEXITSTATUS(status) : -1);
                /* Wait for file change to rebuild and relaunch */
            }
        }

        if (check_changes()) {
            printf("[dev] Rebuilding...\n");
            kill_app();

            ret = cmd_build(0, NULL);
            if (ret != 0) {
                fprintf(stderr, "[dev] Rebuild failed, waiting for next change...\n");
            } else {
                printf("[dev] Relaunching app...\n");
                g_app_pid = launch_app();
            }

            /* Re-scan files (new files may have been added) */
            g_watch_count = 0;
            scan_dir("src");
        }

        usleep(500000); /* 500ms */
    }

    return 0;
}
