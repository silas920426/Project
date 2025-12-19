//API 
const API_URL = "https://monarchistic-organizationally-magdalene.ngrok-free.dev/api/sensor-data"; 

// JWT token
const token = localStorage.getItem("authToken");

if (!token) {
    alert("請先登入！");
    window.location.href = "/login";
}

// 1. 新增：用來記錄上一次的按鈕狀態 (避免一直重複跳視窗)
let lastBtnState = -1; 

// 時間格式化函式
function formatToTWTime(utcStr) {
    if (!utcStr) return "--";
    let isoStr = utcStr.replace(" ", "T") + "Z";
    let date = new Date(isoStr);
    return date.toLocaleString('zh-TW', { hour12: false });
}

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

    // 簡單判斷有效座標 (排除 0,0)
    if (Math.abs(lat) < 0.1 && Math.abs(lng) < 0.1) return;

    if (!marker) {
        marker = L.marker([lat, lng]).addTo(map);
    } else {
        marker.setLatLng([lat, lng]);
    }
    map.setView([lat, lng], 13);
}


//==================================================
// 📊 Chart.js 初始化
//==================================================
const ctx = document.getElementById("sensorChart").getContext("2d");

const sensorChart = new Chart(ctx, {
    type: "line",
    data: {
        labels: [],
        datasets: [
            {
                label: "溫度 (°C)",
                borderColor: "#e67e22", // 改用比較顯眼的顏色
                backgroundColor: "rgba(230, 126, 34, 0.1)",
                data: [],
                fill: true,
                tension: 0.4 // 讓線條圓滑一點
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
        maintainAspectRatio: false, // 讓圖表適應容器高度
        scales: {
            y: {
                beginAtZero: false
            }
        }
    }
});

//==================================================
// 🔄 UI 更新函式 (統一處理畫面刷新)
//==================================================
function updateDashboard(data) {

    if (!data || data.length === 0) return;

    const last = data[data.length - 1];

    if (last.btn === 1) {
        // 為了避免畫面還沒畫好就被 Alert 卡住，稍微延遲 0.1 秒
        setTimeout(() => {
            alert("⚠️ 警告：有人按下按鈕！");
        }, 100);
        }

    // 1. 更新即時數值面板 (首頁)
    if(document.getElementById("temp")) document.getElementById("temp").innerText = last.temp;
    if(document.getElementById("hum")) document.getElementById("hum").innerText = last.hum;
    if(document.getElementById("sat")) document.getElementById("sat").innerText = last.sat ?? "--";
    if(document.getElementById("lat")) document.getElementById("lat").innerText = last.lat ?? "--";
    if(document.getElementById("lng")) document.getElementById("lng").innerText = last.lng ?? "--";
    if(document.getElementById("timestamp")) document.getElementById("timestamp").innerText = formatToTWTime(last.timestamp);
    if(document.getElementById("btn")) document.getElementById("btn").innerText = last.btn === 1 ? "按下" : "未按";
    // ★ 2. 更新溫濕度大面板 (新增的功能)
    if(document.getElementById("big-temp")) document.getElementById("big-temp").innerText = last.temp;
    if(document.getElementById("big-hum")) document.getElementById("big-hum").innerText = last.hum;


    // 3. 更新表格 (顯示最新的 20 筆，最新的在最上面)
    const tbody = document.querySelector("#dataTable tbody");
    if(tbody) {
        tbody.innerHTML = "";
        // 複製陣列並反轉，取前 20 筆
        const tableData = [...data].reverse().slice(0, 20);
        
        tableData.forEach(item => {
            const row = document.createElement("tr");
            let localTime = formatToTWTime(item.timestamp);
            row.innerHTML = `
                <td>${localTime}</td>
                <td>${item.temp}</td>
                <td>${item.hum}</td>
                <td>${item.lat ?? "--"}</td>
                <td>${item.lng ?? "--"}</td>
                <td>${item.sat ?? "--"}</td>
                <td>${item.btn === 1 ? "按下" : "-"}</td>
            `;
            tbody.appendChild(row);
        });
    }

    // 4. 更新地圖
    updateMap(last.lat, last.lng);
    
    // 5. 更新圖表 (使用全部回傳的 50 筆資料畫趨勢)
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

//==================================================
// 📡 SSE 連線設定 (取代 setInterval)
//==================================================
function startStream() {
    console.log("嘗試建立 SSE 連線...");
    const evtSource = new EventSource(`/stream?token=${token}`);

    // 當收到後端推送的資料時
    evtSource.onmessage = function(event) {
        try {
            const data = JSON.parse(event.data);
            console.log("收到更新資料，筆數：", data.length);
            updateDashboard(data);
        } catch (e) {
            console.error("資料解析錯誤:", e);
        }
    };

    // 連線錯誤處理
    evtSource.onerror = function(err) {
        console.error("SSE 連線中斷或錯誤:", err);
        evtSource.close();
        // 5秒後嘗試重連
        setTimeout(startStream, 5000);
    };
}

// 啟動 SSE 監聽
startStream();

// ========== 頁面載入時的第一次資料抓取  ==========
fetch(API_URL, {
    method: "GET",
    headers: {
        "Authorization": "Bearer " + token, //  加入 Header
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