# LightShell — Desktop app framework
# Build: make
# Run:   make run-demo
# Clean: make clean

# === r8e Engine (auto-fetched) ===
ENGINE_REPO    = https://github.com/lightshell-dev/r8e.git
ENGINE_VERSION = v0.1.0
ENGINE_DIR     = engine/r8e

# === Compiler ===
UNAME_S := $(shell uname -s)
CC      ?= clang
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic \
           -I$(ENGINE_DIR)/src/gpu -I$(ENGINE_DIR)/src -I$(ENGINE_DIR)/include

R8E_LIB    = $(ENGINE_DIR)/build/libr8e.a
BUILD_DIR  = build

# === Platform-specific ===
ifeq ($(UNAME_S),Linux)
  OBJCFLAGS    =
  LDFLAGS      = -lvulkan -lX11 -lm
  SRCS_PLATFORM = src/platform_linux.c src/gpu_vulkan.c
  SRCS_C        = src/image.c src/text.c src/glyph_atlas.c src/api_fs.c src/api_sysinfo.c
  SRCS_MAIN     = src/main_linux.c
else ifeq ($(UNAME_S),Darwin)
  OBJCFLAGS    = -fobjc-arc
  LDFLAGS      = -framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore
  SRCS_PLATFORM = src/main.m src/platform_darwin.m src/gpu_metal.m \
                  src/api_clipboard_darwin.m src/api_shell_darwin.m \
                  src/api_dialog_darwin.m src/api_menu_darwin.m
  SRCS_C        = src/image.c src/text.c src/glyph_atlas.c src/api_fs.c src/api_sysinfo.c
  SRCS_MAIN     =
endif

# === Object files ===
SRCS_R8E_DL = $(ENGINE_DIR)/src/gpu/r8e_display_list.c

ifeq ($(UNAME_S),Linux)
  OBJS_PLATFORM = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS_PLATFORM))
  OBJS_MAIN     = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS_MAIN))
  OBJS_C        = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS_C))
  OBJS_R8E      = $(BUILD_DIR)/r8e_display_list.o
  ALL_OBJS      = $(OBJS_MAIN) $(OBJS_PLATFORM) $(OBJS_C) $(OBJS_R8E)
else ifeq ($(UNAME_S),Darwin)
  OBJS_OBJC     = $(patsubst src/%.m,$(BUILD_DIR)/%.o,$(SRCS_PLATFORM))
  OBJS_C        = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS_C))
  OBJS_R8E      = $(BUILD_DIR)/r8e_display_list.o
  ALL_OBJS      = $(OBJS_OBJC) $(OBJS_C) $(OBJS_R8E)
endif

# === Targets ===
.PHONY: all clean run-demo engine shaders

all: $(BUILD_DIR)/lightshell-demo

# --- Auto-fetch r8e engine ---
$(ENGINE_DIR):
	@echo "[lightshell] Fetching r8e engine $(ENGINE_VERSION)..."
	@mkdir -p engine
	@git clone --depth 1 --branch $(ENGINE_VERSION) $(ENGINE_REPO) $(ENGINE_DIR)
	@echo "[lightshell] r8e engine fetched."

# --- Build r8e library ---
$(R8E_LIB): | $(ENGINE_DIR)
	@echo "[lightshell] Building r8e engine..."
	@cd $(ENGINE_DIR) && make release
	@echo "[lightshell] r8e engine built."

engine: $(R8E_LIB)

# --- Compile GLSL shaders (Linux only, run manually) ---
shaders:
	cd src/shaders && sh compile_shaders.sh

# --- Link demo binary ---
$(BUILD_DIR)/lightshell-demo: $(ALL_OBJS) $(R8E_LIB)
	$(CC) $(LDFLAGS) -o $@ $^

# --- Compile rules ---
$(BUILD_DIR)/%.o: src/%.m | $(BUILD_DIR) $(ENGINE_DIR)
	$(CC) $(CFLAGS) $(OBJCFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR) $(ENGINE_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/r8e_display_list.o: $(SRCS_R8E_DL) | $(BUILD_DIR) $(ENGINE_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --- Run ---
run-demo: $(BUILD_DIR)/lightshell-demo
	cd $(dir $<) && ./$(notdir $<)

# --- Clean ---
clean:
	rm -rf $(BUILD_DIR)

# Remove fetched engine too
distclean: clean
	rm -rf engine
