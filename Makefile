
CC = clang
CFLAGS = -Wall -Wextra -O2 -g

BUILD_DIR = build
SRC_DIR = src
TARGET = $(BUILD_DIR)/mdriver

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

.DEFAULT_GOAL := all
.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# Pattern rule to compile any .c file into a .o file in the build directory
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mdriver.o: src/fsecs.h src/fcyc.h src/clock.h src/memlib.h src/config.h src/mm.h
$(BUILD_DIR)/memlib.o: src/memlib.h
$(BUILD_DIR)/mm.o: src/mm.h src/memlib.h
$(BUILD_DIR)/fsecs.o: src/fsecs.h src/config.h
$(BUILD_DIR)/fcyc.o: src/fcyc.h
$(BUILD_DIR)/ftimer.o: src/ftimer.h src/config.h
$(BUILD_DIR)/clock.o: src/clock.h

clean:
	rm -rf $(BUILD_DIR)