# Que programa usar para compilar 
CC = gcc

# -Wall -Wextra Ativar todos os avisos 
# -std=c99  Usar standard C 
# -pthread   Suporte para threads 
# -lrt Biblioteca para semáforos e memória partilhada
# -g  Incluir informação para debug
CFLAGS = -Wall -Wextra -std=c99 -pthread -lrt -g

# -O2    Otimização máxima 
# -DNDEBUG   Desativar checks de debug
RELEASE_FLAGS = -O2 -DNDEBUG

# -O0   Sem otimização 
# -g    Informação de debug
# -DDEBUG  Ativar código de debug
DEBUG_FLAGS = -O0 -g -DDEBUG


# Pasta com código fonte
SRCDIR = src

# Pasta para ficheiros objeto temporários
OBJDIR = obj

# Nome do programa final
TARGET = server





# SOURCES: Encontrar TODOS os ficheiros .c na pasta src
SOURCES = $(wildcard $(SRCDIR)/*.c)

# OBJECTS: Converter nomes .c para .o na pasta obj
# Exemplo: src/main.c → obj/main.o
OBJECTS = $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)




# Target padrão: 'make' ou 'make all'
all: $(TARGET)

# Versão otimizada: 'make release'
release: CFLAGS += $(RELEASE_FLAGS)
release: $(TARGET)

# Versão de desenvolvimento: 'make debug'
debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(TARGET)

# Regra principal: criar o executável final
$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

# Regra para ficheiros objeto: compilar .c para .o
$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Criar pasta obj se não existir
$(OBJDIR):
	mkdir -p $(OBJDIR)



# Limpar: 'make clean' - apaga ficheiros compilados
clean:
	rm -rf $(OBJDIR) $(TARGET) *.log logs/*.log

# Executar: 'make run' - compila e executa
run: $(TARGET)
	./$(TARGET)

# Teste rápido: 'make test' - compila, executa e testa
test: $(TARGET)
	./$(TARGET) &
	sleep 2
	curl http://localhost:8080/
	pkill -f $(TARGET)

# Verificar memory leaks: 'make valgrind'
valgrind: $(TARGET)
	valgrind --leak-check=full --track-origins=yes ./$(TARGET)

# Detetar erros de threads: 'make helgrind'
helgrind: $(TARGET)
	valgrind --tool=helgrind ./$(TARGET)

# Instalar dependências: 'make deps'
deps:
	sudo apt-get update
	sudo apt-get install -y build-essential gcc make
	sudo apt-get install -y apache2-utils curl valgrind gdb

# Declarar targets que não são ficheiros
.PHONY: all clean run test valgrind helgrind deps release debug