from flask import Flask, request, jsonify, render_template, redirect, Response, stream_with_context
from flask_cors import CORS
import sqlite3
import time
import json

print(">>> Flask 檔案載入成功")

app = Flask(__name__, static_folder="static", template_folder="templates")
CORS(app)

DB_NAME = "sensor.db"

# ========== 1. 建立資料庫連線 ==========
def get_db():
    conn = sqlite3.connect(DB_NAME, timeout=30, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn

# ========== 上傳資料 API ==========
@app.route("/api/sensor-data", methods=["POST"])
def upload():
    data = request.json
    if not data:
        return jsonify({"status": "error", "message": "No data received"}), 400

    btn_status = data.get("button")

    conn = get_db()
    try:
        c = conn.cursor()
        c.execute("""
            INSERT INTO sensor_data (temp, hum, lat, lng, sat, btn)
            VALUES (?, ?, ?, ?, ?, ?)
        """, (
            data.get("temp"),
            data.get("hum"),
            data.get("lat"),
            data.get("lng"),
            data.get("sat"),
            btn_status
        ))
        conn.commit()
    except Exception as e:
        print(f"寫入資料庫失敗: {e}")
        conn.rollback() 
        return jsonify({"status": "error", "message": str(e)}), 500
    finally:
        conn.close()

    response_data = {"status": "success"}
    
    if btn_status == 1:
        print(">>> 偵測到警報！發送蜂鳴器啟動指令給 Gateway")
        response_data["command"] = "BUZZER_ON"
    else:
        response_data["command"] = "NONE"

    return jsonify(response_data)

# ========== SSE 即時串流路由 (核心修改) ==========
@app.route('/stream')
def stream():
    def generate():
        last_sent_id = -1
        
        while True:
            conn = get_db()
            try:
                c = conn.cursor()
                # 1. 檢查目前資料庫最新的 ID
                row = c.execute("SELECT MAX(id) as max_id FROM sensor_data").fetchone()
                current_max_id = row["max_id"] if row["max_id"] is not None else 0

                # 2. 如果發現新資料 (ID 變大)
                if current_max_id > last_sent_id:
                    # 取出最新的 50 筆資料 (先取最新的，再轉回正序)
                    sql = """
                    SELECT * FROM (
                        SELECT * FROM sensor_data ORDER BY id DESC LIMIT 50
                    ) ORDER BY id ASC
                    """
                    rows = c.execute(sql).fetchall()
                    data_list = [dict(r) for r in rows]

                    # 3. 轉成 SSE 格式推送
                    json_data = json.dumps(data_list)
                    yield f"data: {json_data}\n\n"
                    
                    # 更新指標
                    last_sent_id = current_max_id
            except Exception as e:
                print(f"Stream Error: {e}")
            finally:
                # 務必關閉連線，避免長時間佔用導致 locked
                conn.close()

            # 每秒檢查一次
            time.sleep(1)

    return Response(stream_with_context(generate()), mimetype='text/event-stream')

# ========== 讀取資料 API (保留給第一次載入或備用) ==========
@app.route("/api/sensor-data", methods=["GET"])
def get_data():
    conn = get_db()
    try:
        c = conn.cursor()
        # 限制只回傳最後 50 筆，避免資料量過大
        sql = """
        SELECT * FROM (
            SELECT * FROM sensor_data ORDER BY id DESC LIMIT 50
        ) ORDER BY id ASC
        """
        rows = c.execute(sql).fetchall()
        return jsonify([dict(row) for row in rows])
    except Exception as e:
        print(f"讀取失敗: {e}")
        return jsonify([]), 500
    finally:
        conn.close()

# ========== 使用者認證路由 (保持不變) ==========
@app.route("/auth/register", methods=["POST"])
def register():
    data = request.json
    username = data.get("username")
    password = data.get("password")

    if not username or not password:
        return jsonify({"message": "帳號或密碼不能空白"}), 400

    conn = get_db()
    try:
        c = conn.cursor()
        exists = c.execute("SELECT * FROM users WHERE username=?", (username,)).fetchone()
        if exists:
            return jsonify({"message": "帳號已存在"}), 400

        c.execute("INSERT INTO users (username, password) VALUES (?, ?)",
                  (username, password))
        conn.commit()
        return jsonify({"message": "註冊成功"}), 200
    except Exception as e:
        return jsonify({"message": "註冊失敗"}), 500
    finally:
        conn.close()

@app.route("/auth/login", methods=["POST"])
def login():
    data = request.json
    username = data.get("username")
    password = data.get("password")

    conn = get_db()
    try:
        c = conn.cursor()
        user = c.execute(
            "SELECT * FROM users WHERE username=? AND password=?",
            (username, password)
        ).fetchone()
        
        if user:
            return jsonify({"message": "登入成功"}), 200
        else:
            return jsonify({"message": "帳號或密碼錯誤"}), 401
    finally:
        conn.close()

# ========== 頁面路由 ==========
@app.route("/")
def root():
    return redirect("/login")   

@app.route("/login")
def login_redirect():
    return render_template("login.html")

@app.route("/register")
def register_page():
    return render_template("register.html")

@app.route("/index")
def index():
    return render_template("index.html")

# ========== 主程式 ==========
if __name__ == "__main__":
    # 設定 WAL 模式 (重要：解決 Locked 問題)
    try:
        init_conn = sqlite3.connect(DB_NAME)
        init_conn.execute("PRAGMA journal_mode=WAL;") 
        print(">>> 資料庫 WAL 模式已啟用")
        init_conn.close()
    except Exception as e:
        print(f"WAL 設定失敗: {e}")

    # 資料表初始化
    conn = sqlite3.connect(DB_NAME)
    c = conn.cursor()

    c.execute("""
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE,
            password TEXT
        )
    """)

    c.execute("""
        CREATE TABLE IF NOT EXISTS sensor_data (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            temp REAL,
            hum REAL,
            lat REAL,
            lng REAL,
            sat INTEGER,
            btn INTEGER,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    """)

    has_user = c.execute("SELECT * FROM users").fetchone()
    if not has_user:
        c.execute("INSERT INTO users (username, password) VALUES (?, ?)",
                  ("admin", "1234"))
        print(">>> 已建立預設帳號：admin / 1234")

    conn.commit()
    conn.close()

    # 開啟 threaded=True 支援多執行緒，讓 SSE 不會卡住其他請求
    app.run(host="0.0.0.0", port=5000, threaded=True)