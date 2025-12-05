let results = [];
let requestCount = 0;

// Função para testar arquivos
async function testFile(path) {
    requestCount++;
    document.getElementById('requestCount').textContent = requestCount;
    
    const startTime = Date.now();
    const resultId = Date.now();
    
    // Adiciona resultado de carregamento
    addResult({
        id: resultId,
        path: path,
        status: 'Loading...',
        time: '...',
        type: '',
        cache: '',
        success: true
    });
    
    try {
        const response = await fetch('/' + path);
        const endTime = Date.now();
        const time = endTime - startTime;
        
        const cache = response.headers.get('X-Cache') || 'N/A';
        const type = response.headers.get('Content-Type') || 'unknown';
        
        updateResult(resultId, {
            status: response.status,
            time: time + 'ms',
            type: type.split(';')[0],
            cache: cache,
            success: response.ok
        });
        
        if (!response.ok) {
            console.warn(`Test failed: ${path} - ${response.status}`);
        }
    } catch (error) {
        updateResult(resultId, {
            status: 'Error',
            time: 'Failed',
            type: 'Network Error',
            cache: 'N/A',
            success: false,
            error: error.message
        });
    }
}

// Função para adicionar resultado
function addResult(data) {
    results.unshift(data);
    renderResults();
}

// Função para atualizar resultado
function updateResult(id, updates) {
    const index = results.findIndex(r => r.id === id);
    if (index !== -1) {
        results[index] = { ...results[index], ...updates };
        renderResults();
    }
}

// Função para renderizar resultados
function renderResults() {
    const container = document.getElementById('resultsList');
    container.innerHTML = '';
    
    results.slice(0, 10).forEach(result => {
        const div = document.createElement('div');
        div.className = 'result-item';
        if (!result.success) div.style.borderLeftColor = '#ff6b6b';
        
        div.innerHTML = `
            <div class="result-path">${result.path}</div>
            <div class="result-details">
                <span>Status: ${result.status}</span>
                <span>Time: ${result.time}</span>
                <span>Type: ${result.type}</span>
                <span class="${result.cache.includes('HIT') ? 'cache-hit' : 'cache-miss'}">
                    Cache: ${result.cache}
                </span>
            </div>
            ${result.error ? `<div style="color:#ff9999;margin-top:8px;font-size:0.9rem">${result.error}</div>` : ''}
        `;
        container.appendChild(div);
    });
}

// Função para executar busca
function performSearch() {
    const input = document.getElementById('searchInput');
    const path = input.value.trim();
    
    if (path) {
        const cleanPath = path.startsWith('/') ? path.substring(1) : path;
        testFile(cleanPath);
        input.value = '';
    } else {
        showModal('Please enter a path to test');
    }
}

// Função para limpar resultados
function clearResults() {
    results = [];
    renderResults();
}

// Funções do modal
function showModal(message) {
    document.getElementById('modalMessage').textContent = message;
    document.getElementById('errorModal').style.display = 'flex';
}

function closeModal() {
    document.getElementById('errorModal').style.display = 'none';
}

// Inicialização
document.addEventListener('DOMContentLoaded', function() {
    // Atualiza timestamp
    document.getElementById('timestamp').textContent = 
        `Server Time: ${new Date().toLocaleString()}`;
    
    // Testa alguns arquivos automaticamente
    setTimeout(() => testFile('index.html'), 1000);
    setTimeout(() => testFile('style.css'), 1500);
    setTimeout(() => testFile('api/data.json'), 2000);
    
    // Fecha modal ao clicar fora
    document.getElementById('errorModal').addEventListener('click', function(e) {
        if (e.target === this) closeModal();
    });
});