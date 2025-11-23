# Makefile
# Inês Batista, Maria Quinteiro

# Sistema de build para o Concurrent HTTP Server.
# Suporta todos os targets obrigatórios: all, clean, run, test.

# =============================================================================
# CONFIGURAÇÕES
# =============================================================================

# Compilador e flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -pthread -lrt -g
TARGET = server

# =============================================================================
# FICHEIROS
# =============================================================================

# Lista de ficheiros fonte


SOURCES = src/main.c \
          src/config.c \
          src/shared_memory.c \
          src/semaphores.c \
          src/master.c \
          src/worker.c

# Gera lista de objetos a partir dos fonte
OBJECTS = $(SOURCES:.c=.o)

# =============================================================================
# REGRAS DE BUILD
# =============================================================================

# Target principal - compila o servidor
$(TARGET): $(OBJECTS)
	@echo "🔗 Linking $(TARGET)..."
	$(CC) $(OBJECTS) -o $(TARGET) $(CFLAGS)
	@echo "✅ Build completed successfully!"

# Regra padrão para compilar .c para .o
%.o: %.c
	@echo "📦 Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# =============================================================================
# TARGETS DE UTILIDADE
# =============================================================================

# Target para limpar ficheiros built
clean:
	@echo "🧹 Cleaning build files..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "✅ Clean completed!"

# Target para build e executar
run: $(TARGET)
	@echo "🚀 Starting server..."
	./$(TARGET)

# Target para testes (placeholder para fase de testes)
test: $(TARGET)
	@echo "🧪 Running tests..."
	# TODO: Implementar testes automáticos
	@echo "✅ Test target ready for implementation"

# Target para mostrar ajuda
help:
	@echo "📖 Available targets:"
	@echo "  all    - Build the server (default)"
	@echo "  clean  - Remove build files"
	@echo "  run    - Build and run the server"
	@echo "  test   - Run automated tests"
	@echo "  help   - Show this help message"

# =============================================================================
# DECLARAÇÕES
# =============================================================================

# Declara targets que não correspondem a ficheiros
.PHONY: all clean run test help