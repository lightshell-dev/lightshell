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

    if (strcmp(tmpl, "svelte") == 0) {
        /* lightshell.json */
        snprintf(path, sizeof(path), "%s/lightshell.json", name);
        char config[1024];
        snprintf(config, sizeof(config),
            "{\n"
            "  \"name\": \"%s\",\n"
            "  \"version\": \"0.1.0\",\n"
            "  \"entry\": \"dist/index.html\",\n"
            "  \"window\": {\n"
            "    \"title\": \"%s\",\n"
            "    \"width\": 1024,\n"
            "    \"height\": 768,\n"
            "    \"minWidth\": 400,\n"
            "    \"minHeight\": 300,\n"
            "    \"resizable\": true\n"
            "  },\n"
            "  \"permissions\": [\"fs\", \"dialog\", \"clipboard\", \"shell\", \"menu\"],\n"
            "  \"devCommand\": \"npm run dev\",\n"
            "  \"buildCommand\": \"npm run build\"\n"
            "}\n", name, name);
        write_file(path, config);

        /* package.json */
        snprintf(path, sizeof(path), "%s/package.json", name);
        char pkg[1024];
        snprintf(pkg, sizeof(pkg),
            "{\n"
            "  \"name\": \"%s\",\n"
            "  \"private\": true,\n"
            "  \"version\": \"0.0.0\",\n"
            "  \"type\": \"module\",\n"
            "  \"scripts\": {\n"
            "    \"dev\": \"vite build --watch\",\n"
            "    \"build\": \"vite build\"\n"
            "  },\n"
            "  \"devDependencies\": {\n"
            "    \"@sveltejs/vite-plugin-svelte\": \"^5.0.0\",\n"
            "    \"svelte\": \"^5.0.0\",\n"
            "    \"vite\": \"^6.0.0\"\n"
            "  }\n"
            "}\n", name);
        write_file(path, pkg);

        /* vite.config.js */
        snprintf(path, sizeof(path), "%s/vite.config.js", name);
        write_file(path,
            "import { defineConfig } from 'vite'\n"
            "import { svelte } from '@sveltejs/vite-plugin-svelte'\n"
            "\n"
            "export default defineConfig({\n"
            "  plugins: [svelte()],\n"
            "  build: {\n"
            "    outDir: 'dist',\n"
            "    emptyOutDir: true,\n"
            "  }\n"
            "})\n");

        /* src/App.svelte */
        snprintf(path, sizeof(path), "%s/src/App.svelte", name);
        write_file(path,
            "<script>\n"
            "  let count = $state(0)\n"
            "  let platform = 'detecting...'\n"
            "\n"
            "  $effect(() => {\n"
            "    if (typeof lightshell !== 'undefined') {\n"
            "      platform = lightshell.system.platform || 'unknown'\n"
            "    }\n"
            "  })\n"
            "\n"
            "  function increment() {\n"
            "    count++\n"
            "  }\n"
            "</script>\n"
            "\n"
            "<main>\n"
            "  <h1>Welcome to {count > 0 ? 'LightShell!' : 'your app'}</h1>\n"
            "  <p>Running on: {platform}</p>\n"
            "  <button onclick={increment}>\n"
            "    Clicked {count} {count === 1 ? 'time' : 'times'}\n"
            "  </button>\n"
            "</main>\n"
            "\n"
            "<style>\n"
            "  main {\n"
            "    font-family: -apple-system, sans-serif;\n"
            "    padding: 24px;\n"
            "    max-width: 600px;\n"
            "  }\n"
            "  h1 {\n"
            "    font-size: 28px;\n"
            "    color: #1a1a1a;\n"
            "    margin-bottom: 8px;\n"
            "  }\n"
            "  p {\n"
            "    color: #666;\n"
            "    margin-bottom: 16px;\n"
            "  }\n"
            "  button {\n"
            "    background: #6366f1;\n"
            "    color: white;\n"
            "    border: none;\n"
            "    padding: 10px 20px;\n"
            "    border-radius: 8px;\n"
            "    font-size: 14px;\n"
            "    cursor: pointer;\n"
            "  }\n"
            "  button:hover {\n"
            "    background: #5558e6;\n"
            "  }\n"
            "</style>\n");

        /* src/main.js */
        snprintf(path, sizeof(path), "%s/src/main.js", name);
        write_file(path,
            "import App from './App.svelte'\n"
            "import { mount } from 'svelte'\n"
            "\n"
            "const app = mount(App, {\n"
            "  target: document.getElementById('app'),\n"
            "})\n"
            "\n"
            "export default app\n");

        /* index.html */
        snprintf(path, sizeof(path), "%s/index.html", name);
        write_file(path,
            "<!DOCTYPE html>\n"
            "<html lang=\"en\">\n"
            "<head>\n"
            "  <meta charset=\"UTF-8\">\n"
            "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
            "  <title>LightShell App</title>\n"
            "</head>\n"
            "<body>\n"
            "  <div id=\"app\"></div>\n"
            "  <script type=\"module\" src=\"/src/main.js\"></script>\n"
            "</body>\n"
            "</html>\n");

        /* .gitignore */
        snprintf(path, sizeof(path), "%s/.gitignore", name);
        write_file(path,
            "node_modules/\n"
            "dist/\n"
            "build/\n"
            ".DS_Store\n");

        printf("\nProject created! Next steps:\n");
        printf("  cd %s\n", name);
        printf("  npm install\n");
        printf("  lightshell dev\n");
    } else {
        /* Default template */
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
    }

    return 0;
}
