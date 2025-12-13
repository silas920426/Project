from flask import Flask, request, jsonify, render_template, redirect
from flask_cors import CORS
import sqlite3

print(">>> Flask 檔案載入成功")

app = Flask(__name__, static_folder="static", template_folder="templates")
CORS(app)

# ========== 建立資料庫連線（加入 timeout + 多執行緒支援） ==========
def get_db():
    conn = sqlite3.connect("sensor.db", timeout=10, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn

# ========== 上傳資料 API ==========
@app.route("/api/sensor-data", methods=["POST"])
def upload():
    data = request.json
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

# ========== 使用者註冊帳號密碼 ==========
@app.route("/auth/register", methods=["POST"])
def register():
    data = request.json
    username = data.get("username")
    password = data.get("password")

    conn = get_db()
    c = conn.cursor()

    if not username or not password:
        return jsonify({"message": "帳號或密碼不能空白"}), 400

    exists = c.execute("SELECT * FROM users WHERE username=?", (username,)).fetchone()
    if exists:
        return jsonify({"message": "帳號已存在"}), 400

    c.execute("INSERT INTO users (username, password) VALUES (?, ?)",
              (username, password))

    conn.commit()
    conn.close()
    return jsonify({"message": "註冊成功"}), 200

# ========== 使用者登入 ==========
@app.route("/auth/login", methods=["POST"])
def login():
    data = request.json
    username = data.get("username")
    password = data.get("password")

    conn = get_db()
    c = conn.cursor()

    user = c.execute(
        "SELECT * FROM users WHERE username=? AND password=?",
        (username, password)
    ).fetchone()

    conn.close()

    if user:
        return jsonify({"message": "登入成功"}), 200
    else:
        return jsonify({"message": "帳號或密碼錯誤"}), 401

# ========== 讀取資料 ==========
@app.route("/api/sensor-data", methods=["GET"])
def get_data():
    conn = get_db()
    c = conn.cursor()
    rows = c.execute("SELECT * FROM sensor_data ORDER BY rowid ASC").fetchall()
    conn.close()
    return jsonify([dict(row) for row in rows])

# ========== 頁面 ==========
@app.route("/")          #網頁進去預設導向 login
def root():
    return redirect("/login")   

@app.route("/login")     #登入頁面
def login_redirect():
    return render_template("login.html")

@app.route("/register")  #註冊頁面
def register_page():
    return render_template("register.html")

@app.route("/index")     #主頁面（Dashboard）
def index():
    return render_template("index.html")

# ========== 主程式 ==========
if __name__ == "__main__":
    conn = sqlite3.connect("sensor.db")
    conn.execute("PRAGMA journal_mode=WAL;")  # ⭐ 啟用 WAL 模式
    c = conn.cursor()

    # 建立資料表
    c.execute("""
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE,
            password TEXT
        )
    """)

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

    # 預設帳號
    has_user = c.execute("SELECT * FROM users").fetchone()
    if not has_user:
        c.execute("INSERT INTO users (username, password) VALUES (?, ?)",
                  ("admin", "1234"))
        print(">>> 已建立預設帳號：admin / 1234")

    conn.commit()
    conn.close()

    app.run(host="0.0.0.0", port=5000)
