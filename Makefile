# Compiler and flags
CC ?= gcc
CFLAGS = -Wall -Wextra -O2 -std=c99
DEBUG_FLAGS = -g -DDEBUG
LDFLAGS = -lm

# Directories
SRC_DIR = .
OBJ_DIR = obj
BIN_DIR = .

# Target executable
TARGET = rulechef

# Source files
SOURCES = main.c buffer.c rule_parser.c hash_tables.c analysis.c processor.c 

# Object files
OBJECTS = $(SOURCES:%.c=$(OBJ_DIR)/%.o)

# Header files
HEADERS = types.h buffer.h rule_parser.h hash_tables.h analysis.h processor.h 

# Default target
all: $(BIN_DIR)/$(TARGET)

# Create object directory
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Link the executable
$(BIN_DIR)/$(TARGET): $(OBJECTS) | $(OBJ_DIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Compile source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Debug build
debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(BIN_DIR)/$(TARGET)

# Clean build files
clean:
	rm -rf $(OBJ_DIR)
	rm -f $(BIN_DIR)/$(TARGET)

# Install (optional)
install: $(BIN_DIR)/$(TARGET)
	cp $(BIN_DIR)/$(TARGET) /usr/local/bin/

# Uninstall (optional)
uninstall:
	rm -f /usr/local/bin/$(TARGET)

# Run tests (you can add test cases here)
test: $(BIN_DIR)/$(TARGET)
	@echo "Running basic tests..."
	@echo "Add your test commands here"

# Show help
help:
	@echo "Available targets:"
	@echo "  all      - Build the program (default)"
	@echo "  debug    - Build with debug symbols"
	@echo "  clean    - Remove build files"
	@echo "  install  - Install to /usr/local/bin"
	@echo "  uninstall- Remove from /usr/local/bin"
	@echo "  test     - Run tests"
	@echo "  help     - Show this help"

# Declare phony targets
.PHONY: all debug clean install uninstall test help

# Dependencies (automatically generated)
-include $(OBJECTS:.o=.d)

# Generate dependency files
$(OBJ_DIR)/%.d: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@