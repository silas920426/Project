// ================== 基本設定 ==================
const API_URL = "/api/sensor-data";
const token = localStorage.getItem("authToken");

// 若未登入直接導回
if (!token) {
    alert("請先登入");
    location.href = "/login";
}

// ================== 時間轉換 ==================
function formatTWTime(ts) {
    if (!ts) return "--";
    const iso = ts.replace(" ", "T") + "Z";
    return new Date(iso).toLocaleString("zh-TW", { hour12: false });
}

// ================== 地圖 ==================
let map = L.map("map").setView([23.5, 121], 7);
L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png").addTo(map);
let marker = null;

function updateMap(lat, lng) {
    if (!lat || !lng) return;
    if (!marker) {
        marker = L.marker([lat, lng]).addTo(map);
    } else {
        marker.setLatLng([lat, lng]);
    }
    map.setView([lat, lng], 14);
}

// ================== Chart ==================
const ctx = document.getElementById("sensorChart");
const sensorChart = new Chart(ctx, {
    type: "line",
    data: {
        labels: [],
        datasets: [
            {
                label: "溫度 °C",
                data: [],
                borderColor: "#e67e22",
                tension: 0.3
            },
            {
                label: "濕度 %",
                data: [],
                borderColor: "#3498db",
                tension: 0.3
            }
        ]
    }
});

// ================== Dashboard 更新 ==================
function updateDashboard(data) {
    if (!data || data.length === 0) return;

    const last = data[data.length - 1];
    const displayName = last.username
        ? `${last.username} (${last.machine_id})`
        : last.machine_id;

    // 即時顯示
    document.getElementById("temp").innerText = last.temp;
    document.getElementById("hum").innerText = last.hum;
    document.getElementById("sat").innerText = last.sat ?? "--";
    document.getElementById("lat").innerText = last.lat ?? "--";
    document.getElementById("lng").innerText = last.lng ?? "--";
    document.getElementById("timestamp").innerText = formatTWTime(last.timestamp);

    const btnEl = document.getElementById("btn");
    btnEl.innerText = last.btn === 1 ? `按下 - ${last.username}` : "未按";
    btnEl.style.color = last.btn === 1 ? "red" : "black";

    document.getElementById("big-temp").innerText = last.temp;
    document.getElementById("big-hum").innerText = last.hum;

    // 地圖
    updateMap(last.lat, last.lng);

    // 表格
    const tbody = document.querySelector("#dataTable tbody");
    tbody.innerHTML = "";
    [...data].reverse().slice(0, 20).forEach(r => {
        tbody.innerHTML += `
            <tr>
                <td>${formatTWTime(r.timestamp)}</td>
                <td>${r.username || r.machine_id}</td>
                <td>${r.temp}</td>
                <td>${r.hum}</td>
                <td>${r.lat ?? "--"}</td>
                <td>${r.lng ?? "--"}</td>
                <td>${r.sat ?? "--"}</td>
                <td style="color:${r.btn === 1 ? "red" : "black"}">
                    ${r.btn === 1 ? "按下" : "-"}
                </td>
            </tr>`;
    });

    // 圖表
    sensorChart.data.labels = [];
    sensorChart.data.datasets[0].data = [];
    sensorChart.data.datasets[1].data = [];

    data.forEach(d => {
        sensorChart.data.labels.push(formatTWTime(d.timestamp).split(" ")[1]);
        sensorChart.data.datasets[0].data.push(d.temp);
        sensorChart.data.datasets[1].data.push(d.hum);
    });

    sensorChart.update();
}

// ================== SSE 連線 ==================
function startStream() {
    const es = new EventSource("/stream");

    es.onmessage = e => {
        const data = JSON.parse(e.data);
        updateDashboard(data);
    };

    es.onerror = () => {
        es.close();
        setTimeout(startStream, 3000);
    };
}

startStream();

// ================== 初始載入 ==================
fetch(API_URL)
    .then(r => r.json())
    .then(updateDashboard);

// ================== 機台註冊 ==================
async function registerMachine() {
    const machineId = document.getElementById("reg-machine-id").value.trim();
    const username = document.getElementById("reg-username").value.trim();

    if (!machineId || !username) {
        alert("請填完整資料");
        return;
    }

    const res = await fetch("/api/register-machine", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ machine_id: machineId, username })
    });

    const data = await res.json();
    alert(data.message);
    loadMachines();
}

// ================== 機台清單 ==================
async function loadMachines() {
    const res = await fetch("/api/machines");
    const data = await res.json();

    const tbody = document.querySelector("#machineTable tbody");
    tbody.innerHTML = "";

    data.forEach(m => {
        tbody.innerHTML += `
            <tr>
                <td><input value="${m.machine_id}" data-old="${m.machine_id}"></td>
                <td><input value="${m.username}"></td>
                <td>
                    <button onclick="saveMachine(this)">儲存</button>
                    <button onclick="deleteMachine('${m.machine_id}')">刪除</button>
                </td>
            </tr>`;
    });
}

// ================== 機台更新 ==================
async function saveMachine(btn) {
    const tr = btn.closest("tr");
    const inputs = tr.querySelectorAll("input");

    const res = await fetch("/api/update-machine", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
            old_machine_id: inputs[0].dataset.old,
            new_machine_id: inputs[0].value,
            username: inputs[1].value
        })
    });

    const data = await res.json();
    alert(data.message);
    loadMachines();
}

// ================== 機台刪除 ==================
async function deleteMachine(id) {
    if (!confirm(`確定刪除 ${id}？`)) return;

    const res = await fetch(`/api/delete-machine/${id}`, {
        method: "DELETE"
    });

    const data = await res.json();
    alert(data.message);
    loadMachines();
}
