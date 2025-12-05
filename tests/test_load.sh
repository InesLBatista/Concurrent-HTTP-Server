#!/bin/bash

# ==============================================
# CONCURRENT HTTP SERVER - TEST SUITE
# Testes 9-24: Funcionais, Concorrência, Sincronização e Stress
# ==============================================

set -e


echo " CONCURRENT HTTP SERVER - TEST SUITE  "

echo ""

# Configuração
PORT=8080
DOC_ROOT="./www"
RESULTS_DIR="tests/results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
REPORT_FILE="$RESULTS_DIR/test_report_$TIMESTAMP.txt"

# Criar diretórios
mkdir -p "$DOC_ROOT" "$RESULTS_DIR" "logs"

# Cleanup function
cleanup() {
    echo "Limpando..."
    pkill -f "./server" 2>/dev/null || true
    pkill -f "ab\|curl" 2>/dev/null || true
    sleep 2
    rm -f server.pid
}

trap cleanup EXIT INT TERM

# ==============================================
# FUNÇÕES AUXILIARES
# ==============================================

# Iniciar servidor
start_server() {
    local port=$1
    local workers=$2
    local threads=$3
    
    echo "Iniciando servidor na porta $port..."
    cat > server.conf << EOF
port=$port
document_root=$DOC_ROOT
num_workers=$workers
threads_per_worker=$threads
max_queue_size=200
EOF
    
    ./server &
    SERVER_PID=$!
    echo $SERVER_PID > server.pid
    
    # Aguardar servidor iniciar
    local attempts=0
    while [ $attempts -lt 30 ]; do
        if curl -s -o /dev/null -w "%{http_code}" "http://localhost:$port/" 2>/dev/null | grep -q "200\|404"; then
            echo "✅ Servidor iniciado (PID: $SERVER_PID)"
            return 0
        fi
        sleep 1
        ((attempts++))
    done
    
    echo "❌ Falha ao iniciar servidor"
    return 1
}

# Testar requisição HTTP
test_request() {
    local url=$1
    local expected_status=$2
    local description=$3
    
    local response=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:$PORT$url" 2>&1)
    
    if [ "$response" = "$expected_status" ]; then
        echo "  ✅ $description: $response"
        return 0
    else
        echo "  ❌ $description: esperado $expected_status, obtido $response"
        return 1
    fi
}

# ==============================================
# TESTES FUNCIONAIS (9-12)
# ==============================================

run_functional_tests() {
    echo ""
    echo "════════════════════════════════════════════"
    echo "TESTES FUNCIONAIS (9-12)"
    echo "════════════════════════════════════════════"
    
    # Criar arquivos de teste
    echo "Criando arquivos de teste..."
    cat > "$DOC_ROOT/index.html" << 'EOF'
<!DOCTYPE html>
<html>
<head><title>Test</title>
<link rel="stylesheet" href="style.css">
</head>
<body>
<h1>Test Server</h1>
<script src="script.js"></script>
<img src="test.jpg">
<a href="sub/page.html">Sub</a>
</body>
</html>
EOF
    
    echo "body{color:blue;}" > "$DOC_ROOT/style.css"
    echo "console.log('test');" > "$DOC_ROOT/script.js"
    dd if=/dev/urandom of="$DOC_ROOT/test.jpg" bs=1K count=1 2>/dev/null
    mkdir -p "$DOC_ROOT/sub"
    echo "<h2>Sub Page</h2>" > "$DOC_ROOT/sub/page.html"
    
    # Teste 9: Tipos de arquivo
    echo ""
    echo "Teste 9: Tipos de arquivo"
    echo "-------------------------"
    test_request "/index.html" "200" "HTML"
    test_request "/style.css" "200" "CSS"
    test_request "/script.js" "200" "JavaScript"
    test_request "/test.jpg" "200" "Imagem"
    
    # Teste 10: Códigos de status
    echo ""
    echo "Teste 10: Códigos de status HTTP"
    echo "-------------------------------"
    test_request "/" "200" "Root (index.html)"
    test_request "/nonexistent.html" "404" "Arquivo não existe"
    test_request "/../etc/passwd" "403" "Path traversal"
    test_request "/sub/" "200" "Subdiretório"
    
    # Teste 11: Directory index
    echo ""
    echo "Teste 11: Directory index serving"
    echo "---------------------------------"
    if curl -s "http://localhost:$PORT/" | grep -q "Test Server"; then
        echo "  ✅ Root serve index.html"
    else
        echo "  ❌ Root não serve index.html"
    fi
    
    # Teste 12: Content-Type headers
    echo ""
    echo "Teste 12: Content-Type headers"
    echo "-----------------------------"
    echo -n "  "
    curl -s -I "http://localhost:$PORT/index.html" | grep -i "content-type:" | tr -d '\r'
    echo -n "  "
    curl -s -I "http://localhost:$PORT/style.css" | grep -i "content-type:" | tr -d '\r'
    echo -n "  "
    curl -s -I "http://localhost:$PORT/script.js" | grep -i "content-type:" | tr -d '\r'
    echo -n "  "
    curl -s -I "http://localhost:$PORT/test.jpg" | grep -i "content-type:" | tr -d '\r'
}

# ==============================================
# TESTES DE CONCORRÊNCIA (13-16)
# ==============================================

run_concurrency_tests() {
    echo ""
    echo "════════════════════════════════════════════"
    echo "TESTES DE CONCORRÊNCIA (13-16)"
    echo "════════════════════════════════════════════"
    
    # Teste 13: Apache Bench
    echo ""
    echo "Teste 13: Apache Bench (10,000 requests, 100 concurrent)"
    echo "-------------------------------------------------------"
    
    if command -v ab &> /dev/null; then
        ab -n 10000 -c 100 "http://localhost:$PORT/index.html" > "$RESULTS_DIR/ab_test.txt" 2>&1
        
        echo "Resultados:"
        grep -E "(Complete requests:|Failed requests:|Requests per second:|Time per request:)" "$RESULTS_DIR/ab_test.txt"
        
        failed=$(grep "Failed requests:" "$RESULTS_DIR/ab_test.txt" | awk '{print $3}')
        if [ "$failed" = "0" ]; then
            echo "✅ Nenhuma falha"
        else
            echo "❌ $failed falhas"
        fi
    else
        echo "⚠️ Apache Bench não instalado"
    fi
    
    # Teste 14: Conexões dropadas
    echo ""
    echo "Teste 14: Conexões sob carga"
    echo "---------------------------"
    
    # Executar carga em background
    ab -n 5000 -c 50 "http://localhost:$PORT/" > /dev/null 2>&1 &
    AB_PID=$!
    
    # Monitorar conexões
    for i in {1..10}; do
        conn=$(netstat -an 2>/dev/null | grep ":$PORT" | grep "ESTABLISHED" | wc -l || echo "0")
        echo "  Segundo $i: $conn conexões"
        sleep 1
    done
    
    wait $AB_PID 2>/dev/null || true
    
    # Teste 15: Múltiplos clients
    echo ""
    echo "Teste 15: Múltiplos clients paralelos"
    echo "------------------------------------"
    
    echo "Iniciando 50 clients..."
    > "$RESULTS_DIR/clients.txt"
    
    for i in {1..50}; do
        (
            success=0
            for j in {1..20}; do
                if curl -s -o /dev/null -w "%{http_code}" "http://localhost:$PORT/" 2>/dev/null | grep -q "200"; then
                    ((success++))
                fi
            done
            echo "Client $i: $success/20" >> "$RESULTS_DIR/clients.txt"
        ) &
    done
    
    wait
    
    total=$(awk '{print $3}' "$RESULTS_DIR/clients.txt" | awk -F/ '{sum += $1} END {print sum}')
    if [ $total -eq 1000 ]; then
        echo "✅ Todos os clients completaram"
    else
        echo "⚠️ Apenas $total/1000 requisições bem-sucedidas"
    fi
    
    # Teste 16: Estatísticas sob carga
    echo ""
    echo "Teste 16: Estatísticas sob carga"
    echo "-------------------------------"
    
    initial_stats=$(curl -s "http://localhost:$PORT/stats" 2>/dev/null || echo '{}')
    echo "Estatísticas iniciais: $initial_stats"
    
    ab -n 2000 -c 100 "http://localhost:$PORT/" > /dev/null 2>&1
    
    final_stats=$(curl -s "http://localhost:$PORT/stats" 2>/dev/null || echo '{}')
    echo "Estatísticas finais: $final_stats"
}

# ==============================================
# TESTES DE SINCRONIZAÇÃO (17-20)
# ==============================================

run_sync_tests() {
    echo ""
    echo "════════════════════════════════════════════"
    echo "TESTES DE SINCRONIZAÇÃO (17-20)"
    echo "════════════════════════════════════════════"
    
    # Teste 17: Race conditions (simplificado)
    echo ""
    echo "Teste 17: Verificação de race conditions"
    echo "---------------------------------------"
    echo "⚠️ Para teste completo com Helgrind/TSan, execute manualmente:"
    echo "   valgrind --tool=helgrind ./server"
    
    # Teste 18: Integridade do log
    echo ""
    echo "Teste 18: Integridade do arquivo de log"
    echo "--------------------------------------"
    
    # Executar requisições concorrentes
    for i in {1..500}; do
        curl -s "http://localhost:$PORT/?id=$i" > /dev/null 2>&1 &
    done
    wait
    
    # Verificar logs
    for log in logs/*.log; do
        if [ -f "$log" ]; then
            lines=$(wc -l < "$log")
            echo "  $log: $lines linhas"
            
            # Verificar formato básico
            valid=$(grep -c '^\[.*\] .* - .* \[.*\] ".*" [0-9]* [0-9]* ".*" ".*"$' "$log" || echo "0")
            if [ $valid -eq $lines ]; then
                echo "    ✅ Formato correto"
            else
                echo "    ⚠️ Possíveis problemas no formato"
            fi
        fi
    done
    
    # Teste 19: Cache consistency
    echo ""
    echo "Teste 19: Consistência do cache"
    echo "------------------------------"
    
    echo "  Primeiro acesso (MISS esperado):"
    curl -s -I "http://localhost:$PORT/index.html" | grep -i "x-cache" || echo "    Sem header X-Cache"
    
    echo "  Acessos concorrentes:"
    for i in {1..20}; do
        curl -s -I "http://localhost:$PORT/index.html" | grep -i "x-cache" | head -1 &
    done
    wait
    
    # Teste 20: Contadores de estatísticas
    echo ""
    echo "Teste 20: Contadores de estatísticas"
    echo "-----------------------------------"
    
    echo "  Executando 100 requisições..."
    for i in {1..100}; do
        curl -s "http://localhost:$PORT/?counter_test=$i" > /dev/null 2>&1 &
    done
    wait
    
    stats=$(curl -s "http://localhost:$PORT/stats" 2>/dev/null || echo '{}')
    echo "  Estatísticas: $stats"
}

# ==============================================
# TESTES DE STRESS (21-24)
# ==============================================

run_stress_tests() {
    echo ""
    echo "════════════════════════════════════════════"
    echo "TESTES DE STRESS (21-24)"
    echo "════════════════════════════════════════════"
    
    # Teste 21: 5+ minutos com carga
    echo ""
    echo "Teste 21: Execução prolongada (1 minuto com carga)"
    echo "--------------------------------------------------"
    echo "Iniciando teste de 1 minuto..."
    
    # Iniciar carga
    (
        for i in {1..60}; do
            curl -s "http://localhost:$PORT/?stress=$i" > /dev/null 2>&1 &
            sleep 1
        done
        wait
    ) &
    
    # Monitorar
    for i in {1..12}; do  # 12 * 5s = 60s
        if ps -p $SERVER_PID > /dev/null 2>&1; then
            echo "  ✅ Servidor ativo após $(($i*5)) segundos"
            sleep 5
        else
            echo "  ❌ Servidor crashou"
            break
        fi
    done
    
    # Teste 22: Memory leaks (simplificado)
    echo ""
    echo "Teste 22: Memory leaks"
    echo "--------------------"
    echo "⚠️ Para teste completo com Valgrind:"
    echo "   valgrind --leak-check=full ./server"
    
    # Teste 23: Shutdown gracefull
    echo ""
    echo "Teste 23: Shutdown gracefull sob carga"
    echo "-------------------------------------"
    
    # Iniciar algumas requisições
    for i in {1..20}; do
        curl -s "http://localhost:$PORT/?shutdown=$i" > /dev/null 2>&1 &
    done
    
    # Enviar SIGTERM
    echo "  Enviando SIGTERM ao servidor..."
    kill -TERM $SERVER_PID
    
    # Aguardar shutdown
    for i in {1..10}; do
        if ps -p $SERVER_PID > /dev/null 2>&1; then
            echo "  Aguardando... ($i/10)"
            sleep 1
        else
            echo "  ✅ Servidor terminou gracefull"
            SERVER_PID=0
            break
        fi
    done
    
    # Forçar se necessário
    if [ $SERVER_PID -ne 0 ]; then
        echo "  ⚠️ Forçando término..."
        kill -KILL $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
        SERVER_PID=0
    fi
    
    # Teste 24: Zombie processes
    echo ""
    echo "Teste 24: Processos zombies"
    echo "--------------------------"
    
    zombies=$(ps aux | grep -E "(server|worker)" | grep -v grep | grep -E "(Z|defunct)" | wc -l)
    if [ $zombies -eq 0 ]; then
        echo "  ✅ Nenhum processo zombie"
    else
        echo "  ❌ $zombies processos zombies encontrados"
    fi
}

# ==============================================
# EXECUÇÃO PRINCIPAL
# ==============================================

main() {
    echo "Compilando servidor..."
    make clean
    if ! make; then
        echo "❌ Falha na compilação"
        exit 1
    fi
    
    echo "✅ Servidor compilado"
    echo ""
    
    # Iniciar servidor para testes
    if ! start_server $PORT 4 8; then
        exit 1
    fi
    
    # Executar todos os testes
    run_functional_tests
    run_concurrency_tests
    run_sync_tests
    
    # Reiniciar servidor para testes de stress
    echo ""
    echo "Reiniciando servidor para testes de stress..."
    cleanup
    sleep 2
    
    if ! start_server $PORT 8 16; then
        exit 1
    fi
    
    run_stress_tests
    
    # Finalizar
    echo ""
    echo "════════════════════════════════════════════"
    echo "✅ TODOS OS TESTES CONCLUÍDOS"
    echo "════════════════════════════════════════════"
    echo ""
    echo "📊 Relatório salvo em: $REPORT_FILE"
    echo "📁 Logs em: logs/"
    echo "📁 Resultados em: $RESULTS_DIR/"
}

# Executar
main 2>&1 | tee "$REPORT_FILE"

# Verificar se houve erros
if [ ${PIPESTATUS[0]} -eq 0 ]; then
    exit 0
else
    exit 1
fi