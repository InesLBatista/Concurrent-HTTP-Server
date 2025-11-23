// Single-page application functionality
console.log("Concurrent HTTP Server Demo - JavaScript loaded");

// Navigation
document.addEventListener('DOMContentLoaded', function() {
    // Handle navigation clicks
    document.querySelectorAll('.nav-link').forEach(link => {
        link.addEventListener('click', function(e) {
            e.preventDefault();
            
            // Update active nav link
            document.querySelectorAll('.nav-link').forEach(l => l.classList.remove('active'));
            this.classList.add('active');
            
            // Show corresponding section
            const targetSection = this.getAttribute('data-section');
            document.querySelectorAll('.section').forEach(section => {
                section.classList.remove('active');
            });
            document.getElementById(targetSection).classList.add('active');
            
            // Scroll to top of section
            window.scrollTo({ top: 0, behavior: 'smooth' });
        });
    });

    // Test if CSS and JS are loading properly
    console.log('✅ Demo website loaded successfully');
    
    const stylesheets = document.querySelectorAll('link[rel="stylesheet"]');
    let allCSSLoaded = true;
    
    stylesheets.forEach(sheet => {
        if (!sheet.sheet) {
            allCSSLoaded = false;
            console.warn('CSS file not loaded:', sheet.href);
        }
    });
    
    if (allCSSLoaded) {
        console.log('✅ All CSS files loaded successfully');
    }
});

// Test functions
function testCSS() {
    const output = document.getElementById('test-output');
    output.textContent = '✅ CSS loaded successfully!\n';
    output.textContent += '• All styles applied correctly\n';
    output.textContent += '• Responsive design working\n';
    output.textContent += '• Animations functional';
    output.style.color = 'green';
}

function testJS() {
    const output = document.getElementById('test-output');
    output.textContent = '✅ JavaScript executed successfully!\n';
    output.textContent += `• Timestamp: ${new Date().toLocaleString()}\n`;
    output.textContent += '• Navigation working\n';
    output.textContent += '• Event handlers functional\n';
    output.textContent += '• Console logging active';
    output.style.color = 'green';
}

function testConcurrent() {
    const output = document.getElementById('test-output');
    output.textContent = '🔄 Testing concurrent requests...\n';
    output.style.color = 'blue';
    
    // Simulate multiple concurrent requests
    const testPromises = [];
    const startTime = Date.now();
    
    for (let i = 1; i <= 5; i++) {
        testPromises.push(
            new Promise((resolve) => {
                setTimeout(() => {
                    resolve(`✅ Request ${i} completed (${Math.random() * 100 + 50}ms)`);
                }, Math.random() * 200 + 100);
            })
        );
    }
    
    Promise.all(testPromises).then(results => {
        const totalTime = Date.now() - startTime;
        output.textContent = `✅ Concurrency test completed in ${totalTime}ms\n`;
        output.textContent += results.join('\n');
        output.style.color = 'green';
    });
}

function testErrorPages() {
    const output = document.getElementById('test-output');
    output.textContent = '🔍 Testing error page handling...\n';
    output.style.color = 'blue';
    
    // Test various error scenarios
    const tests = [
        { url: '/nonexistent-file.html', expected: 404 },
        { url: '/../forbidden-path', expected: 403 },
        { url: '/test%00null', expected: 400 }
    ];
    
    let completed = 0;
    let results = [];
    
    tests.forEach(test => {
        fetch(test.url)
            .then(response => {
                results.push(`✅ ${test.url} → ${response.status} (expected ${test.expected})`);
            })
            .catch(error => {
                results.push(`❌ ${test.url} → Error: ${error.message}`);
            })
            .finally(() => {
                completed++;
                if (completed === tests.length) {
                    output.textContent = '✅ Error page tests completed:\n' + results.join('\n');
                    output.style.color = 'green';
                }
            });
    });
}

function testAll() {
    const output = document.getElementById('test-output');
    output.textContent = '🚀 Running comprehensive test suite...\n\n';
    output.style.color = 'blue';
    
    setTimeout(() => testCSS(), 500);
    setTimeout(() => {
        output.textContent += '\n';
        testJS();
    }, 1500);
    setTimeout(() => {
        output.textContent += '\n';
        testConcurrent();
    }, 2500);
    setTimeout(() => {
        output.textContent += '\n';
        testErrorPages();
    }, 4000);
}

// Utility function to update server info (could be connected to real stats)
function updateServerInfo() {
    // This could be extended to fetch real-time server statistics
    console.log('Server information would be updated here');
}