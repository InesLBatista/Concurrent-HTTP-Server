CC = gcc
CFLAGS = -Wall -Wextra -Werror -pthread -lrt -std=c11
TARGET = server
SRCDIR = src
OBJDIR = obj

SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

# Default build (release)
all: release

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET) tests/test_concurrent *.log *.out www/access.log*

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	@chmod +x tests/test_load.sh
	@./tests/test_load.sh

# Debug build
debug: CFLAGS += -g -fsanitize=thread
debug: clean $(TARGET)

# Release build
release: CFLAGS += -O3
release: $(TARGET)

valgrind: debug
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --log-file=valgrind.log ./$(TARGET)

helgrind: debug
	valgrind --tool=helgrind --log-file=helgrind.log ./$(TARGET)

benchmark: $(TARGET)
	@echo "Starting load test..."
	@if command -v ab >/dev/null 2>&1; then \
		ab -n 1000 -c 100 http://localhost:8080/; \
	else \
		echo "Apache Bench (ab) not installed. Install with: sudo apt-get install apache2-utils"; \
	fi

install_deps:
	sudo apt-get update
	sudo apt-get install -y build-essential gcc make valgrind apache2-utils curl

test_concurrent: tests/test_concurrent.c
	$(CC) $(CFLAGS) -o tests/test_concurrent tests/test_concurrent.c

.PHONY: all clean run test valgrind helgrind benchmark install_deps test_concurrent debug release