from flask import Flask, request, jsonify, render_template
from flask_cors import CORS
import sqlite3

print(">>> Flask 檔案載入成功")

app = Flask(__name__, static_folder="static", template_folder="templates")
CORS(app)

# ========== 建立資料庫連線 ==========
def get_db():
    print(">>> 建立資料庫連線")
    conn = sqlite3.connect("sensor.db")
    conn.row_factory = sqlite3.Row
    return conn


# ========== 上傳資料 API ==========
@app.route("/api/sensor-data", methods=["POST"])
def upload():
    print(">>> 收到 POST")
    data = request.json
    print("收到資料：", data)

    conn = get_db()
    c = conn.cursor()

    c.execute("""
        INSERT INTO sensor_data (temp, hum, timestamp, lat, lng, sat)
        VALUES (?, ?, ?, ?, ?, ?)
    """, (
        data.get("temp"),
        data.get("hum"),
        data.get("timestamp"),
        data.get("lat"),
        data.get("lng"),
        data.get("sat")
    ))

    conn.commit()
    conn.close()
    return jsonify({"status": "success"})


# ========== 讀取資料 API ==========
@app.route("/api/sensor-data", methods=["GET"])
def get_data():
    print(">>> 收到 GET")
    conn = get_db()
    c = conn.cursor()

    c.execute("""
        CREATE TABLE IF NOT EXISTS sensor_data (
            temp REAL,
            hum REAL,
            timestamp TEXT,
            lat REAL,
            lng REAL,
            sat INTEGER
        )
    """)

    rows = c.execute("SELECT * FROM sensor_data ORDER BY rowid ASC").fetchall()
    conn.close()

    return jsonify([dict(row) for row in rows])


# ========== 首頁：顯示 index.html ==========
@app.route("/")
def index():
    print(">>> 進入首頁")
    return render_template("index.html")


# ========== 主程式 ==========
if __name__ == "__main__":
    print(">>> 伺服器正在啟動...")

    conn = get_db()
    c = conn.cursor()
    c.execute("""
        CREATE TABLE IF NOT EXISTS sensor_data (
            temp REAL,
            hum REAL,
            timestamp TEXT,
            lat REAL,
            lng REAL,
            sat INTEGER
        )
    """)
    conn.commit()
    conn.close()

    print(">>> Flask app.run() 執行")
    app.run(host="0.0.0.0", port=5000, debug=True)
