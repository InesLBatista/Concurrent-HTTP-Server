# Inicialização dos Semáforos no Modelo Produtor-Consumidor

A inicialização dos semáforos é um passo crítico que transforma a nossa
estrutura de dados na memória partilhada (`shared_data_t`) num mecanismo
de fila seguro e eficiente, seguindo o padrão **Produtor-Consumidor**.

Usamos a função:

    sem_init(sem_t *sem, int pshared, unsigned int value)

O argumento **pshared = 1** indica que o semáforo será partilhado entre
múltiplos processos (e não apenas threads), o que é essencial para o
modelo **Master-Worker**.



## 1. `mutex` --- Exclusão Mútua

**Finalidade:**\
Garantir que apenas um processo (Master ou Worker) pode aceder e
modificar a fila de conexões ou as estatísticas do servidor a qualquer
momento, evitando condições de corrida.

**Inicialização:**

``` c
sem_init(&g_shared_data->mutex, 1, 1);
```

**Valor Inicial (1):**\
O valor é 1 porque o mutex funciona como um cadeado. Estar a 1 significa
que a secção crítica está livre e pronta para ser ocupada pelo primeiro
processo.



## 2. `empty_slots` --- Slots Vazios / Sincronização do Produtor

**Finalidade:**\
Contar o número de posições vazias disponíveis na fila.\
O Master (Produtor) espera neste semáforo se a fila estiver cheia.

**Inicialização:**

``` c
sem_init(&g_shared_data->empty_slots, 1, MAX_QUEUE_SIZE);
```

**Valor Inicial (MAX_QUEUE_SIZE):**\
Inicializamos com a capacidade máxima da fila (100, definida por
`MAX_QUEUE_SIZE`).\
Como a fila começa completamente vazia, todos os slots estão
disponíveis.



## 3. `full_slots` --- Slots Preenchidos / Sincronização do Consumidor

**Finalidade:**\
Contar o número de conexões (itens) que estão atualmente na fila e
prontas para serem processadas.\
O Worker (Consumidor) espera neste semáforo se a fila estiver vazia.

**Inicialização:**

``` c
sem_init(&g_shared_data->full_slots, 1, 0);
```

**Valor Inicial (0):**\
Inicializamos com 0 porque a fila começa vazia.\
O Worker espera neste semáforo.\
O Master incrementa este valor após colocar uma conexão na fila,
sinalizando a existência de trabalho.



# Porque é que o `master.c` só implementa o erro **503**?

A razão pela qual o `master.c` (o processo Produtor) só implementa o
erro **503** e não os outros é simples e intencional.





## 1. O Master só lida com a Sobrecarga do Sistema (**503**)

O Master é responsável por gerir a fila de conexões.\
O erro **503 --- Service Unavailable** corresponde a um problema de
**sobrecarga do servidor**, que o Master pode detetar imediatamente.

### Lógica:

-   O Master tenta executar `sem_trywait` no semáforo `empty_slots`.\
-   Se o valor for **zero**, significa que **a fila está cheia**.\
-   Assim, o sistema está sobrecarregado e o Master **não deve aceitar
    mais conexões**.

### Ação:

-   O Master envia **503 Service Unavailable**.
-   Fecha a conexão.
-   Garante que a fila não cresce para além da sua capacidade.

O Master funciona como um **porteiro**:\
se há espaço, deixa entrar; se não há, devolve 503.



## 2. Os Workers lidam com os Erros de Protocolo (**4xx** e **5xx**)

Os restantes erros (400, 403, 404, 500, 501) **dependem do conteúdo do
pedido HTTP**, que o Master não lê nem processa.

Quem lê e interpreta o pedido são os **Workers (Consumidores)**.

  -----------------------------------------------------------------------
  Tipo de Erro            Quando ocorre             Responsável
  ----------------------- ------------------------- ---------------------
  **4xx --- Erros do      O Worker lê o pedido e    **Worker**
  Cliente**               descobre que é inválido   
                          (400), proibido (403) ou  
                          que o recurso não existe  
                          (404).                    

  **5xx --- Erros do      O Worker tenta executar a **Worker**
  Servidor**              lógica e encontra erro    
                          interno (500) ou método   
                          não implementado (501).   
  -----------------------------------------------------------------------

O Master apenas passa o file descriptor (`client_fd`).\
É o Worker que:

-   lê a requisição HTTP,
-   valida o método e o caminho,
-   executa a ação,
-   escolhe o código de resposta.



## Conclusão

O Master deve permanecer **simples**:\
- aceita conexões,\
- coloca-as na fila,\
- devolve **503** se não houver espaço.

Tudo o resto --- parsing HTTP, validação, erros 4xx/5xx --- pertence aos
**Workers**, onde serão implementados nos próximos passos.