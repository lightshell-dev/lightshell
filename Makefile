UNAME_S := $(shell uname -s)
CC ?= clang
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -I../r8e/src/gpu -I../r8e/include

# r8e JS engine library
R8E_LIB = ../r8e/build/libr8e.a
SRCS_R8E = ../r8e/src/gpu/r8e_display_list.c
BUILD_DIR = build

# Platform-specific sources and flags
ifeq ($(UNAME_S),Linux)
  OBJCFLAGS =
  LDFLAGS = -lvulkan -lX11 -lm
  SRCS_PLATFORM = src/platform_linux.c src/gpu_vulkan.c
  SRCS_C = src/image.c src/text.c src/glyph_atlas.c src/api_fs.c src/api_sysinfo.c
  # Linux main.c (no ObjC)
  SRCS_MAIN = src/main_linux.c
else ifeq ($(UNAME_S),Darwin)
  OBJCFLAGS = -fobjc-arc
  LDFLAGS = -framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore
  SRCS_PLATFORM = src/main.m src/platform_darwin.m src/gpu_metal.m \
                  src/api_clipboard_darwin.m src/api_shell_darwin.m \
                  src/api_dialog_darwin.m src/api_menu_darwin.m
  SRCS_C = src/image.c src/text.c src/glyph_atlas.c src/api_fs.c src/api_sysinfo.c
  SRCS_MAIN =
endif

# Object files
ifeq ($(UNAME_S),Linux)
  OBJS_PLATFORM = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS_PLATFORM))
  OBJS_MAIN = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS_MAIN))
  OBJS_C = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS_C))
  OBJS_R8E = $(BUILD_DIR)/r8e_display_list.o
  ALL_OBJS = $(OBJS_MAIN) $(OBJS_PLATFORM) $(OBJS_C) $(OBJS_R8E)
else ifeq ($(UNAME_S),Darwin)
  OBJS_OBJC = $(patsubst src/%.m,$(BUILD_DIR)/%.o,$(SRCS_PLATFORM))
  OBJS_C = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRCS_C))
  OBJS_R8E = $(BUILD_DIR)/r8e_display_list.o
  ALL_OBJS = $(OBJS_OBJC) $(OBJS_C) $(OBJS_R8E)
endif

.PHONY: all clean run-demo r8e-lib shaders

all: $(BUILD_DIR)/lightshell-demo

# Build r8e library if needed
r8e-lib:
	cd ../r8e && make release

# Compile GLSL shaders to SPIR-V C headers (Linux only, run manually or before build)
shaders:
	cd src/shaders && sh compile_shaders.sh

$(BUILD_DIR)/lightshell-demo: $(ALL_OBJS) $(R8E_LIB)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: src/%.m | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJCFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/r8e_display_list.o: ../r8e/src/gpu/r8e_display_list.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run-demo: $(BUILD_DIR)/lightshell-demo
	cd $(dir $<) && ./$(notdir $<)

clean:
	rm -rf $(BUILD_DIR)
