CC = cc
CFLAGS = -std=c11 -Wall -Wextra -O2
BUILD_DIR = ../build

SRCS = main.c cmd_init.c cmd_dev.c cmd_build.c cmd_doctor.c

.PHONY: all clean install

all: $(BUILD_DIR)/lightshell

$(BUILD_DIR)/lightshell: $(SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -f $(BUILD_DIR)/lightshell

install: $(BUILD_DIR)/lightshell
	cp $(BUILD_DIR)/lightshell /usr/local/bin/lightshell
	@echo "Installed to /usr/local/bin/lightshell"
