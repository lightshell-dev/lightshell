CC = clang
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -I../r8e/src/gpu -I../r8e/include
OBJCFLAGS = -fobjc-arc
LDFLAGS = -framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore

SRCS_OBJC = src/main.m src/platform_darwin.m src/gpu_metal.m
SRCS_R8E = ../r8e/src/gpu/r8e_display_list.c

# r8e JS engine library
R8E_LIB = ../r8e/build/libr8e.a

BUILD_DIR = build
OBJS_OBJC = $(patsubst src/%.m,$(BUILD_DIR)/%.o,$(SRCS_OBJC))
OBJS_R8E = $(BUILD_DIR)/r8e_display_list.o

.PHONY: all clean run-demo r8e-lib

all: $(BUILD_DIR)/lightshell-demo

# Build r8e library if needed
r8e-lib:
	cd ../r8e && make release

$(BUILD_DIR)/lightshell-demo: $(OBJS_OBJC) $(OBJS_R8E) $(R8E_LIB)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: src/%.m | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJCFLAGS) -c -o $@ $<

$(BUILD_DIR)/r8e_display_list.o: ../r8e/src/gpu/r8e_display_list.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run-demo: $(BUILD_DIR)/lightshell-demo
	cd $(dir $<) && ./$(notdir $<)

clean:
	rm -rf $(BUILD_DIR)
