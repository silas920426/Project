// API 
const API_URL = "/api/sensor-data"; // 建議用相對路徑，避免 ngrok 網址變動問題

// JWT token
const token = localStorage.getItem("authToken");

if (!token) {
    alert("請先登入！");
    window.location.href = "/login";
}

// 時間格式化函式
function formatToTWTime(utcStr) {
    if (!utcStr) return "--";
    let isoStr = utcStr.replace(" ", "T") + "Z";
    let date = new Date(isoStr);
    return date.toLocaleString('zh-TW', { hour12: false });
}

// 🌍 Map 初始化
let map = L.map('map').setView([23.5, 120.5], 8);
L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19
}).addTo(map);

let marker = null;

// 地圖更新
function updateMap(lat, lng) {
    if (!lat || !lng) return;

    // 簡單判斷有效座標 (排除 0,0)
    if (Math.abs(lat) < 0.1 && Math.abs(lng) < 0.1) return;

    if (!marker) {
        marker = L.marker([lat, lng]).addTo(map);
    } else {
        marker.setLatLng([lat, lng]);
    }
    map.setView([lat, lng], 13);
}

// 📊 Chart.js 初始化
const ctx = document.getElementById("sensorChart").getContext("2d");

const sensorChart = new Chart(ctx, {
    type: "line",
    data: {
        labels: [],
        datasets: [
            {
                label: "溫度 (°C)",
                borderColor: "#e67e22",
                backgroundColor: "rgba(230, 126, 34, 0.1)",
                data: [],
                fill: true,
                tension: 0.4
            },
            {
                label: "濕度 (%)",
                borderColor: "#3498db",
                backgroundColor: "rgba(52, 152, 219, 0.1)",
                data: [],
                fill: true,
                tension: 0.4
            }
        ]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
            y: {
                beginAtZero: false
            }
        }
    }
});

// 🔄 頁面更新函式 (統一處理畫面刷新)
function updateDashboard(data) {

    if (!data || data.length === 0) return;

    const last = data[data.length - 1];

    //計算顯示名稱 (優先顯示使用者名稱，沒有則顯示機台ID)
    const displayName = last.username ? `${last.username} (${last.machine_id})` : (last.machine_id || "未知裝置");

    //警報邏輯
    const lastAlertedTime = localStorage.getItem("lastAlertTimestamp");
    
    if (last.btn === 1 && last.timestamp !== lastAlertedTime) {
        // 更新 localStorage，防止重複跳出
        localStorage.setItem("lastAlertTimestamp", last.timestamp);

        setTimeout(() => {
            // 顯示詳細的使用者資訊
            alert(`⚠️ 緊急通知 ⚠️\n\n使用者：${displayName}\n已按下求救按鈕！`);
        }, 100);
    }

    // 3. 更新即時數值面板 
    if(document.getElementById("temp")) document.getElementById("temp").innerText = last.temp;
    if(document.getElementById("hum")) document.getElementById("hum").innerText = last.hum;
    if(document.getElementById("sat")) document.getElementById("sat").innerText = last.sat ?? "--";
    if(document.getElementById("lat")) document.getElementById("lat").innerText = last.lat ?? "--";
    if(document.getElementById("lng")) document.getElementById("lng").innerText = last.lng ?? "--";
    if(document.getElementById("timestamp")) document.getElementById("timestamp").innerText = formatToTWTime(last.timestamp);
    
    // [修改] 按鈕狀態顯示使用者名稱
    if(document.getElementById("btn")) {
        const statusText = last.btn === 1 ? "按下" : "未按";
        document.getElementById("btn").innerText = last.btn === 1 ? `${statusText} - ${displayName}` : statusText;
        document.getElementById("btn").style.color = last.btn === 1 ? "red" : "black";
    }

    // 更新溫濕度大面板 
    if(document.getElementById("big-temp")) document.getElementById("big-temp").innerText = last.temp;
    if(document.getElementById("big-hum")) document.getElementById("big-hum").innerText = last.hum;


    // 4. 更新表格 (顯示最新的 20 筆)
    const tbody = document.querySelector("#dataTable tbody");
    if(tbody) {
        tbody.innerHTML = "";
        const tableData = [...data].reverse().slice(0, 20);
        
        tableData.forEach(item => {
            const row = document.createElement("tr");
            let localTime = formatToTWTime(item.timestamp);
            
            // [新增] 表格顯示使用者欄位
            let userDisplay = item.username || item.machine_id || "--";

            row.innerHTML = `
                <td>${localTime}</td>
                <td>${userDisplay}</td> <td>${item.temp}</td>
                <td>${item.hum}</td>
                <td>${item.lat ?? "--"}</td>
                <td>${item.lng ?? "--"}</td>
                <td>${item.sat ?? "--"}</td>
                <td style="color:${item.btn===1?'red':'black'}">${item.btn === 1 ? "按下" : "-"}</td>
            `;
            tbody.appendChild(row);
        });
    }

    // 5. 更新地圖
    updateMap(last.lat, last.lng);
    
    // 6. 更新圖表
    sensorChart.data.labels = [];
    sensorChart.data.datasets[0].data = [];
    sensorChart.data.datasets[1].data = [];

    data.forEach(d => {
        let fullTime = formatToTWTime(d.timestamp); 
        let timeOnly = fullTime.split(" ")[1]; 
        sensorChart.data.labels.push(timeOnly);
        sensorChart.data.datasets[0].data.push(d.temp);
        sensorChart.data.datasets[1].data.push(d.hum);
    });

    sensorChart.update();
}

// 📡 SSE 連線設定
function startStream() {
    console.log("嘗試建立 SSE 連線...");
    const evtSource = new EventSource(`/stream?token=${token}`);

    evtSource.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            console.log("收到更新資料，筆數：", data.length);
            updateDashboard(data);
        } catch (e) {
            console.error("資料解析錯誤:", e);
        }
    };

    evtSource.onerror = function(err) {
        console.error("SSE 連線中斷或錯誤:", err);
        evtSource.close();
        setTimeout(startStream, 5000);
    };
}

startStream();

// 機台註冊
async function registerMachine() {
    const machineId = document.getElementById("reg-machine-id").value.trim();
    const username = document.getElementById("reg-username").value.trim();

    if (!machineId || !username) {
        alert("請輸入完整資訊");
        return;
    }

    try {
        const res = await fetch("/api/register-machine", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
                // 不需要 Authorization header
            },
            body: JSON.stringify({ machine_id: machineId, username: username })
        });
        
        const data = await res.json();
        alert(data.message);
        if (res.ok) {
            document.getElementById("reg-machine-id").value = "";
            document.getElementById("reg-username").value = "";
        }
    } catch (err) {
        console.error(err);
        alert("註冊失敗");
    }
}

// ========== 頁面載入時的第一次資料抓取 ==========
fetch(API_URL, {
    method: "GET",
    headers: {
        "Authorization": "Bearer " + token,
        "Content-Type": "application/json"
    }
})
.then(res => {
    if (res.status === 401 || res.status === 403) {
        alert("登入逾時，請重新登入");
        window.location.href = "/login";
        return [];
    }
    return res.json();
})
.then(data => updateDashboard(data))
.catch(err => console.error("Fetch Error:", err));