// ================= 基本設定 =================
const API_URL = "/api/sensor-data";

// 僅前端登入狀態
if (!localStorage.getItem("loggedIn")) {
    alert("請先登入");
    window.location.href = "/login";
}

// ================= 時間轉台灣 =================
function formatToTWTime(utcStr) {
    if (!utcStr) return "--";
    const iso = utcStr.replace(" ", "T") + "Z";
    return new Date(iso).toLocaleString("zh-TW", { hour12: false });
}

// ================= 地圖 =================
let map = L.map("map").setView([23.7, 121], 7);
L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 18
}).addTo(map);

let marker = null;

function updateMap(lat, lng) {
    if (!lat || !lng) return;
    if (Math.abs(lat) < 0.1 && Math.abs(lng) < 0.1) return;

    if (!marker) {
        marker = L.circleMarker([lat, lng], {
            radius: 8,
            color: "red",
            fillColor: "red",
            fillOpacity: 0.8
        }).addTo(map);
    } else {
        marker.setLatLng([lat, lng]);
    }

    map.setView([lat, lng], 14);
}

// ================= 折線圖 =================
const ctx = document.getElementById("sensorChart").getContext("2d");

const sensorChart = new Chart(ctx, {
    type: "line",
    data: {
        labels: [],
        datasets: [
            {
                label: "溫度 (°C)",
                data: [],
                borderColor: "#e67e22",
                tension: 0.3
            },
            {
                label: "濕度 (%)",
                data: [],
                borderColor: "#3498db",
                tension: 0.3
            }
        ]
    },
    options: {
        responsive: true,
        maintainAspectRatio: false
    }
});

// ================= 去除重複警報 =================
function handleAlert(last) {
    if (last.btn !== 1) return;

    const lastHandledId = localStorage.getItem("lastHandledAlertId");

    // 第一次載入頁面只記錄，不跳警報
    if (!lastHandledId) {
        localStorage.setItem("lastHandledAlertId", last.id);
        return;
    }

    // 只處理新的事件
    if (Number(last.id) > Number(lastHandledId)) {
        localStorage.setItem("lastHandledAlertId", last.id);

        const displayName = last.username
            ? `${last.username} (${last.machine_id})`
            : last.machine_id || "未知裝置";
        
        alert(`⚠️ 緊急警報\n\n使用者: ${last.username || "未知"}\n機台ID: ${last.machine_id}\n\n已按下求救按鈕`);
    }
}

// ================= 更新整個 Dashboard =================
function updateDashboard(data) {
    if (!data || data.length === 0) return;

    const last = data[data.length - 1];
    const displayName = last.username
        ? `${last.username} (${last.machine_id})`
        : last.machine_id || "未知裝置";

    // --- 即時數據 ---
    document.getElementById("temp").innerText = last.temp;
    document.getElementById("hum").innerText = last.hum;
    document.getElementById("sat").innerText = last.sat ?? "--";
    document.getElementById("lat").innerText = last.lat ?? "--";
    document.getElementById("lng").innerText = last.lng ?? "--";
    document.getElementById("timestamp").innerText = formatToTWTime(last.timestamp);

    const btnEl = document.getElementById("btn");
    if (last.btn === 1) {
        btnEl.innerText = `按下 - ${last.username}`;
        btnEl.style.color = "red";
    } else {
        btnEl.innerText = "未按";
        btnEl.style.color = "black";
    }

    // --- 環境監測面板更新 ---
    document.getElementById("big-temp").innerText = last.temp;
    document.getElementById("big-hum").innerText = last.hum;

    // --- 歷史紀錄面板更新 ---
    const tbody = document.querySelector("#dataTable tbody");
    tbody.innerHTML = "";

    [...data].reverse().slice(0, 20).forEach(r => {
        const tr = document.createElement("tr");
        tr.innerHTML = `
            <td>${formatToTWTime(r.timestamp)}</td>
            <td>${r.username || r.machine_id}</td>
            <td>${r.temp}</td>
            <td>${r.hum}</td>
            <td>${r.lat ?? "--"}</td>
            <td>${r.lng ?? "--"}</td>
            <td>${r.sat ?? "--"}</td>
            <td style="color:${r.btn === 1 ? "red" : "black"}">
                ${r.btn === 1 ? "按下" : "-"}
            </td>
        `;
        tbody.appendChild(tr);
    });

    // --- 地圖 ---
    updateMap(last.lat, last.lng);

    // --- 趨勢圖表更新 ---
    sensorChart.data.labels = data.map(d =>
        formatToTWTime(d.timestamp).split(" ")[1]
    );
    sensorChart.data.datasets[0].data = data.map(d => d.temp);
    sensorChart.data.datasets[1].data = data.map(d => d.hum);
    sensorChart.update();

    // 去除重複警報
    handleAlert(last);
}

// ================= SSE 即時更新 =================
function startStream() {
    const es = new EventSource("/stream");

    es.onmessage = e => {
        const data = JSON.parse(e.data);
        updateDashboard(data);
    };

    es.onerror = () => {
        es.close();
        setTimeout(startStream, 5000);
    };
}

startStream();

// ================= 機台管理 =================
async function registerMachine() {
    const machineId = document.getElementById("reg-machine-id").value.trim();
    const username = document.getElementById("reg-username").value.trim();

    if (!machineId || !username) {
        alert("請填完整");
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
// ================= 載入機台資訊 ===========
async function loadMachines() {
    const res = await fetch("/api/machines");
    const data = await res.json();

    const tbody = document.querySelector("#machineTable tbody");
    tbody.innerHTML = "";

    data.forEach(m => {
        const tr = document.createElement("tr");
        tr.innerHTML = `
            <td><input value="${m.machine_id}" data-old="${m.machine_id}"></td>
            <td><input value="${m.username}"></td>
            <td>
                <button onclick="saveMachine(this)">儲存</button>
                <button onclick="deleteMachine('${m.machine_id}')">刪除</button>
            </td>
        `;
        tbody.appendChild(tr);
    });
}
//==================== 更新機台資訊 =================
async function saveMachine(btn) {
    const row = btn.closest("tr");
    const inputs = row.querySelectorAll("input");

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
//==================== 刪除機台資訊 =================
async function deleteMachine(id) {
    if (!confirm(`刪除 ${id}？`)) return;
    const res = await fetch(`/api/delete-machine/${id}`, { method: "DELETE" });
    const data = await res.json();
    alert(data.message);
    loadMachines();
}

// ================= 初次載入 =================
fetch(API_URL)
    .then(r => r.json())
    .then(updateDashboard);
