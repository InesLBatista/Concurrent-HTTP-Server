# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -pthread -lrt -g
TARGET = module_tests

# Source files with correct paths
SRC_DIR = src
SRC = $(SRC_DIR)/main.c $(SRC_DIR)/config.c $(SRC_DIR)/http.c $(SRC_DIR)/logger.c $(SRC_DIR)/stats.c

# Object files
OBJ = $(SRC:.c=.o)

# Default target
all: $(TARGET)

# Build the test executable
$(TARGET): $(OBJ)
	$(CC) -o $(TARGET) $(OBJ) $(CFLAGS)
	@echo "✅ Build successful! Run ./$(TARGET) to test all modules"

# Compile source files to object files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Run the tests
test: $(TARGET)
	@echo "🧪 Running comprehensive module tests..."
	./$(TARGET)

# Clean build files
clean:
	rm -f $(TARGET) $(OBJ) test_access.log test_server.conf

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: $(TARGET)

.PHONY: all test clean debug