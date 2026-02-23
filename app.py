from flask import Flask, request, jsonify, render_template, redirect, Response, stream_with_context
from flask_cors import CORS
import sqlite3, time, json, jwt, datetime
from pyngrok import ngrok

app = Flask(__name__, static_folder="static", template_folder="templates")
CORS(app)

DB_NAME = "sensor.db"
SECRET_KEY = "my_super_secret_iot_key_2025"

# ========= DB =========
def get_db():
    conn = sqlite3.connect(DB_NAME, timeout=30, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn

# ========= Gateway 上傳資料到資料庫（JWT token 驗證） =========
@app.route("/api/sensor-data", methods=["POST"])
def upload():
    auth = request.headers.get("Authorization")
    if not auth:
        return jsonify({"message": "Missing token"}), 401

    try:
        _, token = auth.split(" ")
        jwt.decode(token, SECRET_KEY, algorithms=["HS256"])
    except Exception:
        return jsonify({"message": "Invalid token"}), 403

    data = request.json

    conn = get_db()
    try:
        conn.execute("""
            INSERT INTO sensor_data (temp, hum, lat, lng, sat, btn, machine_id)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        """, (
            data.get("temp"),
            data.get("hum"),
            data.get("lat"),
            data.get("lng"),
            data.get("sat"),
            data.get("button"),
            data.get("machine_id")
        ))
        conn.commit()
    finally:
        conn.close()

    # 回傳指令給 Gateway
    if data.get("button") == 1:
        return jsonify({
            "status": "success",
            "command": "BUZZER_ON"
        })
    else:
        return jsonify({
            "status": "success",
            "command": "NONE"
        })


# =========  讀取資料SQL =========
SELECT_SQL = """
SELECT s.*, m.username
FROM sensor_data s
LEFT JOIN machines m ON s.machine_id = m.machine_id
ORDER BY s.id DESC LIMIT 50
"""

# ========= SSE 即時連線(判斷是否有新資料) =========
@app.route("/stream")
def stream():
    def generate():
        last_id = -1
        while True:
            conn = get_db()
            try:
                max_id = conn.execute("SELECT MAX(id) FROM sensor_data").fetchone()[0] or 0
                if max_id > last_id:
                    rows = conn.execute(
                        f"SELECT * FROM ({SELECT_SQL}) ORDER BY id ASC"
                    ).fetchall()
                    yield f"data: {json.dumps([dict(r) for r in rows])}\n\n"
                    last_id = max_id
            finally:
                conn.close()
            time.sleep(1)
    return Response(stream_with_context(generate()), mimetype="text/event-stream")

# ========= 網頁取得資料 =========
@app.route("/api/sensor-data", methods=["GET"])
def get_data():
    conn = get_db()
    try:
        rows = conn.execute(
            f"SELECT * FROM ({SELECT_SQL}) ORDER BY id ASC"
        ).fetchall()
        return jsonify([dict(r) for r in rows])
    finally:
        conn.close()

# ========= 使用者登入 =========
@app.route("/auth/login", methods=["POST"])
def login():
    data = request.json
    conn = get_db()
    try:
        user = conn.execute(
            "SELECT * FROM users WHERE username=? AND password=?",
            (data["username"], data["password"])
        ).fetchone()
        if user:
            return jsonify({"message": "登入成功"}), 200
        return jsonify({"message": "帳密錯誤"}), 401
    finally:
        conn.close()

# ========= 機台管理 =========
@app.route("/api/machines")
def machines():
    conn = get_db()
    try:
        rows = conn.execute("SELECT * FROM machines").fetchall()
        return jsonify([dict(r) for r in rows])
    finally:
        conn.close()

# ========= 註冊機台 =========
@app.route("/api/register-machine", methods=["POST"])
def register_machine():
    d = request.json
    conn = get_db()
    try:
        conn.execute(
            "REPLACE INTO machines (machine_id, username) VALUES (?,?)",
            (d["machine_id"], d["username"])
        )
        conn.commit()
        return jsonify({"message": "註冊成功"})
    finally:
        conn.close()

# ========= 更新機台 =========
@app.route("/api/update-machine", methods=["PUT"])
def update_machine():
    d = request.json
    conn = get_db()
    try:
        conn.execute("DELETE FROM machines WHERE machine_id=?", (d["old_machine_id"],))
        conn.execute(
            "INSERT INTO machines VALUES (?,?)",
            (d["new_machine_id"], d["username"])
        )
        conn.commit()
        return jsonify({"message": "更新成功"})
    finally:
        conn.close()

# ========= 刪除機台 =========
@app.route("/api/delete-machine/<mid>", methods=["DELETE"])
def delete_machine(mid):
    conn = get_db()
    try:
        conn.execute("DELETE FROM machines WHERE machine_id=?", (mid,))
        conn.commit()
        return jsonify({"message": "已刪除"})
    finally:
        conn.close()

# ========= 頁面路由 =========
@app.route("/") #網址預設
def root():
    return redirect("/login")

@app.route("/login") #login頁面
def login_page():
    return render_template("login.html")

@app.route("/index") #主頁面
def index():
    return render_template("index.html")

# ========= 主程式 =========
if __name__ == "__main__":

    conn = sqlite3.connect(DB_NAME)
    conn.execute("PRAGMA journal_mode=WAL;")
    conn.execute("""
        CREATE TABLE IF NOT EXISTS sensor_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            temp REAL, hum REAL,
            lat REAL, lng REAL,
            sat INTEGER, btn INTEGER,
            machine_id TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    """)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS machines (
            machine_id TEXT PRIMARY KEY,
            username TEXT
        )
    """)
    conn.execute("""
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE,
            password TEXT
        )
    """)

    # 檢查並建立預設管理員帳號 (admin / 1234)
    # 使用 SELECT 檢查 admin 是否已存在
    cursor = conn.execute("SELECT COUNT(*) FROM users WHERE username = ?", ("admin",))
    count = cursor.fetchone()[0]

    if count == 0:
        conn.execute("INSERT INTO users (username, password) VALUES (?, ?)", ("admin", "1234"))
        print(">>> [初始化] 偵測到無使用者，已建立預設帳號: admin / 1234")
    else:
        print(">>> [初始化] 管理員帳號已存在。")


    conn.commit()
    conn.close()

    app.run(host="0.0.0.0", port=5000, threaded=True)
