#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int check_command(const char *cmd, const char *label) {
    char buf[256];
    snprintf(buf, sizeof(buf), "%s > /dev/null 2>&1", cmd);
    int ret = system(buf);
    if (ret == 0) {
        printf("  ✓ %s\n", label);
        return 1;
    } else {
        printf("  ✗ %s\n", label);
        return 0;
    }
}

static int check_file(const char *path, const char *label) {
    if (access(path, F_OK) == 0) {
        printf("  ✓ %s\n", label);
        return 1;
    } else {
        printf("  ✗ %s (%s not found)\n", label, path);
        return 0;
    }
}

int cmd_doctor(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("LightShell Doctor\n");
    printf("=================\n\n");

    int ok = 0, total = 0;

    /* Compiler */
    printf("Compiler:\n");
    total++; ok += check_command("cc --version", "C compiler (cc)");
    total++; ok += check_command("clang --version", "Clang");

    /* Platform */
    printf("\nPlatform:\n");
    #ifdef __APPLE__
    total++; ok += check_command("xcrun --find metal", "Metal compiler");
    total++; ok += check_file("/System/Library/Frameworks/Metal.framework", "Metal framework");
    total++; ok += check_file("/System/Library/Frameworks/Cocoa.framework", "Cocoa framework");
    #else
    total++; ok += check_command("pkg-config --exists vulkan", "Vulkan");
    total++; ok += check_command("pkg-config --exists x11", "X11");
    #endif

    /* Text rendering */
    printf("\nText rendering:\n");
    total++; ok += check_command("pkg-config --exists harfbuzz", "HarfBuzz");
    total++; ok += check_command("pkg-config --exists freetype2", "FreeType");

    /* r8e engine */
    printf("\nr8e engine:\n");
    total++; ok += check_file("../r8e/build/libr8e.a", "libr8e.a");

    printf("\n%d/%d checks passed\n", ok, total);

    if (ok < total) {
        printf("\nTo fix missing dependencies:\n");
        printf("  brew install harfbuzz freetype   # macOS\n");
        printf("  cd ../r8e && make release         # build r8e\n");
    }

    return ok < total ? 1 : 0;
}
