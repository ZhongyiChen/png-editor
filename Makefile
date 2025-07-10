CC = gcc
# CFLAGS = -Wall -Wextra -O2 -I. -g
CFLAGS = -Wall -Wextra -O2 -I.
LDFLAGS = -lz -lgdi32 -lcomdlg32

# Directories
TMP_DIR = ./tmp
BUILD_DIR = ./dist
TARGET = png_viewer.exe

# Cross-platform commands
# ifeq ($(OS),Windows_NT)
# 	MKDIR = mkdir
# 	RM = del /Q
# 	CP = copy /Y
# else
# 	MKDIR = mkdir -p
# 	RM = rm -f
# 	CP = cp
# endif

# MSYS2 已经实现了 Linux 开发完全体，无需再区分系统环境
MKDIR = mkdir -p
RM = rm -f
CP = cp

# Targets
all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(TMP_DIR)/png_viewer.o $(TMP_DIR)/png_decoder.o | $(BUILD_DIR)
	$(CC) -o $@ $^ $(LDFLAGS)
	@if [ -d "libs" ]; then $(CP) libs/*.dll $(BUILD_DIR)/; fi

$(TMP_DIR)/%.o: %.c | $(TMP_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TMP_DIR) $(BUILD_DIR):
	$(MKDIR) "$@"

clean:
	$(RM) $(TMP_DIR)/*.o $(BUILD_DIR)/$(TARGET) $(BUILD_DIR)/*.dll

.PHONY: clean all
