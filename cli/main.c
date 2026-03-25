#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Forward declarations */
int cmd_init(int argc, char **argv);
int cmd_dev(int argc, char **argv);
int cmd_build(int argc, char **argv);
int cmd_doctor(int argc, char **argv);

static void print_usage(void) {
    printf("LightShell - Desktop app framework\n\n");
    printf("Usage: lightshell <command> [options]\n\n");
    printf("Commands:\n");
    printf("  init <name>     Create a new LightShell project\n");
    printf("  dev             Start development server with hot reload\n");
    printf("  build           Build native binary\n");
    printf("  doctor          Check system dependencies\n");
    printf("\nOptions:\n");
    printf("  init --template default|react|svelte\n");
    printf("  build --target darwin|linux\n");
}

int main(int argc, char **argv) {
    if (argc < 2) { print_usage(); return 1; }

    const char *cmd = argv[1];
    if (strcmp(cmd, "init") == 0) return cmd_init(argc - 2, argv + 2);
    if (strcmp(cmd, "dev") == 0) return cmd_dev(argc - 2, argv + 2);
    if (strcmp(cmd, "build") == 0) return cmd_build(argc - 2, argv + 2);
    if (strcmp(cmd, "doctor") == 0) return cmd_doctor(argc - 2, argv + 2);
    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage(); return 0;
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    print_usage();
    return 1;
}
