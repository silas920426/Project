const API_URL = "https://monarchistic-organizationally-magdalene.ngrok-free.dev/api/sensor-data";

async function loadData() {
    try {
        const res = await fetch(API_URL);
        const data = await res.json();

        if (data.length === 0) return;

        // 最新一筆
        const last = data[data.length - 1];

        document.getElementById("temp").innerText = last.temp;
        document.getElementById("hum").innerText = last.hum;
        document.getElementById("sat").innerText = last.sat ?? "--";
        document.getElementById("lat").innerText = last.lat ?? "--";
        document.getElementById("lng").innerText = last.lng ?? "--";
        document.getElementById("timestamp").innerText = last.timestamp;

        // ======= 更新表格 =======
        const tbody = document.querySelector("#dataTable tbody");
        tbody.innerHTML = "";

        data.slice(-20).reverse().forEach(item => { // 只顯示最新 20 筆
            const row = document.createElement("tr");
            row.innerHTML = `
                <td>${item.timestamp}</td>
                <td>${item.temp}</td>
                <td>${item.hum}</td>
                <td>${item.lat ?? "--"}</td>
                <td>${item.lng ?? "--"}</td>
                <td>${item.sat ?? "--"}</td>
            `;
            tbody.appendChild(row);
        });

    } catch (err) {
        console.error("Failed to load:", err);
    }
}

// 每 3 秒更新
setInterval(loadData, 10000);
loadData();
