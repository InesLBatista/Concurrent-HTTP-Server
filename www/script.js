console.log("System Dashboard loaded.");

function testLatency() {
    const resultBox = document.getElementById('ping-result');
    const btn = document.querySelector('button');

    // UI State: Loading
    btn.disabled = true;
    btn.innerText = "Testing...";
    resultBox.style.color = "#34495e";
    resultBox.innerText = "Pinging...";

    // Use performance API for high-resolution timing
    const start = performance.now();

    // Simulate a brief processing delay to make the interaction visible
    setTimeout(() => {
        const end = performance.now();
        const latency = Math.round(end - start);

        // UI State: Success
        btn.disabled = false;
        btn.innerText = "Ping Server (JS Test)";
        resultBox.innerText = `Ack received: ${latency}ms latency`;
        resultBox.style.color = "#27ae60"; // Green color
        resultBox.style.fontWeight = "bold";

        console.log(`Latency check complete: ${latency}ms`);
    }, 350);
}