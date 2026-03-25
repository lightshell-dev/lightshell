#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) { fprintf(stderr, "Error: cannot create %s\n", path); return; }
    fputs(content, f);
    fclose(f);
}

static void mkdirp(const char *path) {
    mkdir(path, 0755);
}

int cmd_init(int argc, char **argv) {
    if (argc < 1) {
        fprintf(stderr, "Usage: lightshell init <project-name>\n");
        return 1;
    }

    const char *name = argv[0];
    const char *tmpl = "default";

    /* Check for --template flag */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) {
            tmpl = argv[i + 1]; i++;
        }
    }

    printf("Creating LightShell project: %s (template: %s)\n", name, tmpl);

    /* Create directories */
    mkdirp(name);
    char path[512];
    snprintf(path, sizeof(path), "%s/src", name); mkdirp(path);
    snprintf(path, sizeof(path), "%s/assets", name); mkdirp(path);

    /* lightshell.json */
    snprintf(path, sizeof(path), "%s/lightshell.json", name);
    char config[1024];
    snprintf(config, sizeof(config),
        "{\n"
        "  \"name\": \"%s\",\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"entry\": \"src/index.html\",\n"
        "  \"window\": {\n"
        "    \"title\": \"%s\",\n"
        "    \"width\": 1024,\n"
        "    \"height\": 768,\n"
        "    \"minWidth\": 400,\n"
        "    \"minHeight\": 300,\n"
        "    \"resizable\": true\n"
        "  },\n"
        "  \"permissions\": [\"fs\", \"dialog\", \"clipboard\", \"shell\", \"menu\"]\n"
        "}\n", name, name);
    write_file(path, config);

    /* src/index.html */
    snprintf(path, sizeof(path), "%s/src/index.html", name);
    char html[2048];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "  <meta charset=\"utf-8\">\n"
        "  <title>%s</title>\n"
        "  <link rel=\"stylesheet\" href=\"style.css\">\n"
        "</head>\n"
        "<body>\n"
        "  <div id=\"app\">\n"
        "    <h1>Welcome to %s</h1>\n"
        "    <p>Built with LightShell</p>\n"
        "    <button id=\"btn\">Click me</button>\n"
        "    <p id=\"output\"></p>\n"
        "  </div>\n"
        "  <script src=\"app.js\"></script>\n"
        "</body>\n"
        "</html>\n", name, name);
    write_file(path, html);

    /* src/app.js */
    snprintf(path, sizeof(path), "%s/src/app.js", name);
    write_file(path,
        "// Your LightShell app\n"
        "var count = 0;\n"
        "var btn = document.querySelector('#btn');\n"
        "var output = document.querySelector('#output');\n"
        "\n"
        "btn.addEventListener('click', function() {\n"
        "  count++;\n"
        "  output.textContent = 'Clicked ' + count + ' times';\n"
        "});\n"
        "\n"
        "// Access native APIs\n"
        "var platform = lightshell.system.platform;\n"
        "console.log('Running on: ' + platform);\n");

    /* src/style.css */
    snprintf(path, sizeof(path), "%s/src/style.css", name);
    write_file(path,
        "* { margin: 0; padding: 0; box-sizing: border-box; }\n"
        "body { font-family: -apple-system, sans-serif; padding: 24px; }\n"
        "h1 { font-size: 28px; margin-bottom: 8px; }\n"
        "p { color: #666; margin-bottom: 16px; }\n"
        "button {\n"
        "  background: #3366FF; color: white; border: none;\n"
        "  padding: 8px 16px; border-radius: 6px; cursor: pointer;\n"
        "  font-size: 14px;\n"
        "}\n"
        "button:hover { background: #5588FF; }\n"
        "#output { margin-top: 16px; font-weight: bold; color: #333; }\n");

    printf("\nProject created! Next steps:\n");
    printf("  cd %s\n", name);
    printf("  lightshell dev\n");
    return 0;
}
