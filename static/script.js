// 後端 API
const API_URL = "https://monarchistic-organizationally-magdalene.ngrok-free.dev/api/sensor-data";

//==================================================
//  🌍 Leaflet Map 初始化
//==================================================
let map = L.map('map').setView([23.5, 120.5], 8);
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19
}).addTo(map);

let marker = null;

function updateMap(lat, lng) {
    if (!lat || !lng) return;

    if (!marker) {
        marker = L.marker([lat, lng]).addTo(map);
    } else {
        marker.setLatLng([lat, lng]);
    }
    map.setView([lat, lng], 13);
}


//==================================================
// 📊 Chart.js：初始化折線圖（溫度 + 溼度）
//==================================================
const ctx = document.getElementById("sensorChart").getContext("2d");

const sensorChart = new Chart(ctx, {
    type: "line",
    data: {
        labels: [],
        datasets: [
            {
                label: "Temperature (°C)",
                borderColor: "red",
                data: [],
                fill: false
            },
            {
                label: "Humidity (%)",
                borderColor: "blue",
                data: [],
                fill: false
            }
        ]
    },
    options: {
        responsive: true,
        scales: {
            y: {
                beginAtZero: false
            }
        }
    }
});


//==================================================
//  📥 取得資料 + 更新地圖 + 更新圖表
//==================================================
async function loadData() {
    try {
        const res = await fetch(API_URL);
        const data = await res.json();
        if (data.length === 0) return;

        const last = data[data.length - 1];

        //----------------------------------
        //   更新即時資料顯示
        //----------------------------------
        document.getElementById("temp").innerText = last.temp;
        document.getElementById("hum").innerText = last.hum;
        document.getElementById("sat").innerText = last.sat ?? "--";
        document.getElementById("lat").innerText = last.lat ?? "--";
        document.getElementById("lng").innerText = last.lng ?? "--";
        document.getElementById("timestamp").innerText = last.timestamp;

        //----------------------------------
        //   更新資料表（顯示最近 20 筆）
        //----------------------------------
        const tbody = document.querySelector("#dataTable tbody");
        tbody.innerHTML = "";

        data.slice(-20).reverse().forEach(item => {
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

        //----------------------------------
        //   更新地圖
        //----------------------------------
        updateMap(last.lat, last.lng);

        //----------------------------------
        //   更新折線圖（最多 30 筆）
        //----------------------------------
        sensorChart.data.labels.push(last.timestamp);
        sensorChart.data.datasets[0].data.push(last.temp);
        sensorChart.data.datasets[1].data.push(last.hum);

        // 只保留最近 30 筆
        if (sensorChart.data.labels.length > 30) {
            sensorChart.data.labels.shift();
            sensorChart.data.datasets[0].data.shift();
            sensorChart.data.datasets[1].data.shift();
        }

        sensorChart.update();

    } catch (err) {
        console.error("Failed to load:", err);
    }
}


// 每 10 秒更新一次（你的原始設定）
setInterval(loadData, 10000);
loadData();
