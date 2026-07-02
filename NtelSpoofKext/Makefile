CC = clang

# Set DEBUG=1 for sparse logging, DEBUG=2 for hot-path logging.
# Example: make DEBUG=2 simulation
# Default is 0 (no debug logging) to match production/release behaviour.
DEBUG   ?= 0
CFLAGS   = -Wall -Wextra -std=c11 -I./include -pthread -DNTEL_DEBUG=$(DEBUG) -DNTEL_USERMODE
LDFLAGS  = -pthread

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

SIM_SRCS = $(SRC_DIR)/NtelSimulation.c \
           $(SRC_DIR)/NtelSharedRing.c \
           $(SRC_DIR)/NtelTranslationEngine.c \
           $(SRC_DIR)/NtelFirmwareInjector.c \
           $(SRC_DIR)/NtelDebugRing.c

SIM_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SIM_SRCS))
DAEMON_SRC = $(SRC_DIR)/ntel_debug_daemon.c
DAEMON_OBJ = $(BUILD_DIR)/ntel_debug_daemon.o
DEBUG_RING_OBJ = $(BUILD_DIR)/NtelDebugRing.o

TARGET = $(BUILD_DIR)/ntel_sim
DAEMON = $(BUILD_DIR)/ntel_debug_daemon

.PHONY: all clean test simulation daemon

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(SIM_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(DAEMON): $(DAEMON_OBJ) $(DEBUG_RING_OBJ)
	$(CC) $(LDFLAGS) $^ -o $@

daemon: $(DAEMON)

test:
	$(MAKE) $(TARGET) DEBUG=2
	./$(TARGET)

simulation:
	$(MAKE) $(TARGET) DEBUG=2
	./$(TARGET) --threads=4 --stress-cache --stress-ring

clean:
	rm -rf $(BUILD_DIR)