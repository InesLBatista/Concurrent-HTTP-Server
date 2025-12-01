// ============================================================================
// CONCURRENT HTTP SERVER - CLIENT-SIDE SCRIPT
// Inês Batista (124877) | Maria Quinteiro (124996)
// ============================================================================

// Global variables
let requestCount = 0;
let serverPort = 8080;

// ============================================================================
// DOM CONTENT LOADED
// ============================================================================
document.addEventListener('DOMContentLoaded', function() {
    console.log('Concurrent HTTP Server - Single Thread page loaded');
    
    // Initialize the page
    initializePage();
    
    // Start updating time
    updateCurrentTime();
    setInterval(updateCurrentTime, 1000);
    
    // Load saved request count from localStorage
    loadRequestCount();
});

// ============================================================================
// INITIALIZE PAGE
// ============================================================================
function initializePage() {
    // Detect server port from URL
    detectServerPort();
    
    // Update all port displays
    updatePortDisplays();
    
    // Add click animations to buttons
    addButtonAnimations();
    
    // Initialize test results container
    initializeTestResults();
}

// ============================================================================
// DETECT SERVER PORT
// ============================================================================
function detectServerPort() {
    const urlParams = new URLSearchParams(window.location.search);
    const portParam = urlParams.get('port');
    
    if (portParam && !isNaN(portParam) && portParam > 0 && portParam <= 65535) {
        serverPort = parseInt(portParam);
    }
    
    // Also check window location
    if (window.location.port && window.location.port !== '') {
        serverPort = parseInt(window.location.port);
    }
}

// ============================================================================
// UPDATE PORT DISPLAYS
// ============================================================================
function updatePortDisplays() {
    // Update server port displays
    const portElements = document.querySelectorAll('#server-port, #curl-port, #curl-port2, #curl-port3, #curl-port4');
    portElements.forEach(element => {
        element.textContent = serverPort;
    });
}

// ============================================================================
// UPDATE CURRENT TIME
// ============================================================================
function updateCurrentTime() {
    const now = new Date();
    const timeString = now.toLocaleTimeString('en-US', {
        hour12: true,
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
    
    const dateString = now.toLocaleDateString('en-US', {
        weekday: 'long',
        year: 'numeric',
        month: 'long',
        day: 'numeric'
    });
    
    const timeElement = document.getElementById('current-time');
    if (timeElement) {
        timeElement.textContent = `${dateString} - ${timeString}`;
    }
}

// ============================================================================
// LOAD REQUEST COUNT
// ============================================================================
function loadRequestCount() {
    const savedCount = localStorage.getItem('serverRequestCount');
    if (savedCount) {
        requestCount = parseInt(savedCount);
    } else {
        requestCount = 0;
    }
    
    updateRequestCountDisplay();
}

// ============================================================================
// SAVE REQUEST COUNT
// ============================================================================
function saveRequestCount() {
    localStorage.setItem('serverRequestCount', requestCount.toString());
}

// ============================================================================
// UPDATE REQUEST COUNT DISPLAY
// ============================================================================
function updateRequestCountDisplay() {
    const countElement = document.getElementById('request-count');
    if (countElement) {
        countElement.textContent = requestCount;
    }
}

// ============================================================================
// INCREMENT REQUEST COUNT
// ============================================================================
function incrementRequestCount() {
    requestCount++;
    updateRequestCountDisplay();
    saveRequestCount();
}

// ============================================================================
// ADD BUTTON ANIMATIONS
// ============================================================================
function addButtonAnimations() {
    const buttons = document.querySelectorAll('.test-btn');
    
    buttons.forEach(button => {
        button.addEventListener('mousedown', function() {
            this.style.transform = 'translateY(1px)';
        });
        
        button.addEventListener('mouseup', function() {
            this.style.transform = 'translateY(0)';
        });
        
        button.addEventListener('mouseleave', function() {
            this.style.transform = 'translateY(0)';
        });
    });
}

// ============================================================================
// INITIALIZE TEST RESULTS
// ============================================================================
function initializeTestResults() {
    const resultsContainer = document.getElementById('test-results');
    if (resultsContainer) {
        resultsContainer.innerHTML = `
            <div class="test-status">
                <p>Click a test button to test server endpoints</p>
            </div>
        `;
    }
}

// ============================================================================
// TEST ENDPOINT
// ============================================================================
function testEndpoint(endpoint) {
    incrementRequestCount();
    
    const resultsContainer = document.getElementById('test-results');
    if (!resultsContainer) return;
    
    // Show loading state
    resultsContainer.innerHTML = `
        <div class="test-loading">
            <div class="loading-spinner"></div>
            <p>Testing ${endpoint}...</p>
        </div>
    `;
    
    // Build the URL
    const url = `http://localhost:${serverPort}${endpoint}`;
    
    // Make the request
    fetch(url, {
        method: 'GET',
        mode: 'no-cors', // Avoid CORS issues for local testing
        cache: 'no-cache'
    })
    .then(response => {
        // Since we're using no-cors mode, we can't access response details
        // But we can check if the request was made
        showTestResult(endpoint, 'success', 'Request sent successfully');
    })
    .catch(error => {
        // This will catch network errors
        if (error.message.includes('Failed to fetch') || 
            error.message.includes('NetworkError')) {
            showTestResult(endpoint, 'error', 'Server not responding. Make sure the server is running on port ' + serverPort);
        } else {
            showTestResult(endpoint, 'error', 'Error: ' + error.message);
        }
    });
    
    // Fallback in case fetch takes too long
    setTimeout(() => {
        if (resultsContainer.innerHTML.includes('Testing')) {
            showTestResult(endpoint, 'warning', 'Request timed out. Server might be busy or not running.');
        }
    }, 5000);
}

// ============================================================================
// SHOW TEST RESULT
// ============================================================================
function showTestResult(endpoint, type, message) {
    const resultsContainer = document.getElementById('test-results');
    if (!resultsContainer) return;
    
    let icon, colorClass, title;
    
    switch (type) {
        case 'success':
            icon = '✅';
            colorClass = 'success';
            title = 'Success';
            break;
        case 'warning':
            icon = '⚠️';
            colorClass = 'warning';
            title = 'Warning';
            break;
        case 'error':
            icon = '❌';
            colorClass = 'error';
            title = 'Error';
            break;
        default:
            icon = 'ℹ️';
            colorClass = 'info';
            title = 'Info';
    }
    
    resultsContainer.innerHTML = `
        <div class="test-result ${colorClass}">
            <div class="result-header">
                <span class="result-icon">${icon}</span>
                <h4>${title}: Testing ${endpoint}</h4>
            </div>
            <div class="result-body">
                <p>${message}</p>
                <div class="result-details">
                    <p><strong>Endpoint:</strong> ${endpoint}</p>
                    <p><strong>URL:</strong> http://localhost:${serverPort}${endpoint}</p>
                    <p><strong>Time:</strong> ${new Date().toLocaleTimeString()}</p>
                </div>
            </div>
            <div class="result-footer">
                <button onclick="testEndpoint('${endpoint}')" class="retry-btn">Retry Test</button>
                <button onclick="copyTestUrl('${endpoint}')" class="copy-btn">Copy URL</button>
            </div>
        </div>
    `;
    
    // Add CSS for the result display
    addResultStyles();
}

// ============================================================================
// ADD RESULT STYLES
// ============================================================================
function addResultStyles() {
    // Check if styles are already added
    if (document.getElementById('result-styles')) return;
    
    const style = document.createElement('style');
    style.id = 'result-styles';
    style.textContent = `
        .test-loading {
            text-align: center;
            padding: 20px;
        }
        
        .loading-spinner {
            width: 40px;
            height: 40px;
            border: 4px solid #f3f4f6;
            border-top: 4px solid #2563eb;
            border-radius: 50%;
            animation: spin 1s linear infinite;
            margin: 0 auto 15px;
        }
        
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
        
        .test-result {
            border-radius: 8px;
            overflow: hidden;
            border: 1px solid;
        }
        
        .test-result.success {
            border-color: #10b981;
            background: #f0fdf4;
        }
        
        .test-result.warning {
            border-color: #f59e0b;
            background: #fffbeb;
        }
        
        .test-result.error {
            border-color: #ef4444;
            background: #fef2f2;
        }
        
        .test-result.info {
            border-color: #3b82f6;
            background: #eff6ff;
        }
        
        .result-header {
            padding: 15px 20px;
            display: flex;
            align-items: center;
            gap: 10px;
            border-bottom: 1px solid;
        }
        
        .success .result-header {
            border-bottom-color: #10b981;
        }
        
        .warning .result-header {
            border-bottom-color: #f59e0b;
        }
        
        .error .result-header {
            border-bottom-color: #ef4444;
        }
        
        .info .result-header {
            border-bottom-color: #3b82f6;
        }
        
        .result-icon {
            font-size: 1.2rem;
        }
        
        .result-header h4 {
            margin: 0;
            font-size: 1rem;
        }
        
        .result-body {
            padding: 20px;
        }
        
        .result-body p {
            margin-bottom: 15px;
        }
        
        .result-details {
            background: white;
            padding: 15px;
            border-radius: 6px;
            margin-top: 15px;
        }
        
        .result-details p {
            margin-bottom: 8px;
            font-size: 0.9rem;
        }
        
        .result-footer {
            padding: 15px 20px;
            display: flex;
            gap: 10px;
            border-top: 1px solid #e5e7eb;
        }
        
        .retry-btn, .copy-btn {
            padding: 8px 16px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-family: inherit;
            font-size: 0.9rem;
            font-weight: 500;
            transition: all 0.2s;
        }
        
        .retry-btn {
            background: #2563eb;
            color: white;
        }
        
        .retry-btn:hover {
            background: #1d4ed8;
        }
        
        .copy-btn {
            background: #6b7280;
            color: white;
        }
        
        .copy-btn:hover {
            background: #4b5563;
        }
    `;
    
    document.head.appendChild(style);
}

// ============================================================================
// COPY TEST URL
// ============================================================================
function copyTestUrl(endpoint) {
    const url = `http://localhost:${serverPort}${endpoint}`;
    
    navigator.clipboard.writeText(url).then(() => {
        // Show success message
        const resultsContainer = document.getElementById('test-results');
        if (resultsContainer) {
            const copyBtn = resultsContainer.querySelector('.copy-btn');
            if (copyBtn) {
                const originalText = copyBtn.textContent;
                copyBtn.textContent = 'Copied!';
                copyBtn.style.background = '#10b981';
                
                setTimeout(() => {
                    copyBtn.textContent = originalText;
                    copyBtn.style.background = '';
                }, 2000);
            }
        }
    }).catch(err => {
        console.error('Failed to copy URL: ', err);
        alert('Failed to copy URL to clipboard');
    });
}

// ============================================================================
// REFRESH PAGE
// ============================================================================
function refreshPage() {
    location.reload();
}

// ============================================================================
// CONSOLE GREETING
// ============================================================================
console.log('%c⚡ Concurrent HTTP Server - Single Thread', 'color: #2563eb; font-size: 16px; font-weight: bold;');
console.log('%cAuthors: Inês Batista (124877) & Maria Quinteiro (124996)', 'color: #7c3aed;');
console.log('%cSistemas Operativos 2024/2025', 'color: #6b7280;');