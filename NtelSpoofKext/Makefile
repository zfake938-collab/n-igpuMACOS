CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I./include -pthread
LDFLAGS = -pthread

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

SIM_SRCS = $(SRC_DIR)/NtelSimulation.c \
           $(SRC_DIR)/NtelSharedRing.c \
           $(SRC_DIR)/NtelTranslationEngine.c \
           $(SRC_DIR)/NtelFirmwareInjector.c

SIM_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SIM_SRCS))

TARGET = $(BUILD_DIR)/ntel_sim

.PHONY: all clean test

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(SIM_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

test: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
