from flask import Flask, request, jsonify, render_template, redirect, Response, stream_with_context
from flask_cors import CORS
import sqlite3
import time
import json
import jwt          
import datetime     
print(">>> Flask 檔案載入成功")

app = Flask(__name__, static_folder="static", template_folder="templates")
CORS(app)

DB_NAME = "sensor.db"

#設定JWT token密鑰
SECRET_KEY = "my_super_secret_iot_key_2025"

# ========== 1. 建立資料庫連線 ==========
def get_db():
    conn = sqlite3.connect(DB_NAME, timeout=30, check_same_thread=False)
    conn.row_factory = sqlite3.Row
    return conn

# ========== 產生 JWT Token 路由 ==========
@app.route("/api/generate-token")
def generate_token():
    # 設定過期時間為 1 年 (IoT 裝置通常很少更換 Token)
    payload = {
        "device": "gateway_001",
        "role": "uploader",
        "exp": datetime.datetime.utcnow() + datetime.timedelta(days=365)
    }
    token = jwt.encode(payload, SECRET_KEY, algorithm="HS256")
    return jsonify({"token": token})


# ========== 上傳資料 API ==========
@app.route("/api/sensor-data", methods=["POST"])
def upload():
    # --- JWT 驗證區塊 ---
    auth_header = request.headers.get('Authorization')
    
    if not auth_header:
        return jsonify({"status": "error", "message": "缺少 Authorization Header"}), 401
    
    try:
        # 格式通常為 "Bearer <token>"
        token_type, token = auth_header.split(" ")
        if token_type != "Bearer":
            raise ValueError("Token type must be Bearer")
            
        # 解碼並驗證
        decoded = jwt.decode(token, SECRET_KEY, algorithms=["HS256"])
        # 如果程式執行到這裡，代表驗證成功
        print(f">>> 驗證成功，裝置來源: {decoded.get('device')}")

    except jwt.ExpiredSignatureError:
        return jsonify({"status": "error", "message": "Token 已過期"}), 401
    except jwt.InvalidTokenError:
        return jsonify({"status": "error", "message": "Token 無效"}), 403
    except Exception as e:
        return jsonify({"status": "error", "message": str(e)}), 401
    # --- [新增] JWT 驗證區塊 結束 ---

    data = request.json
    if not data:
        return jsonify({"status": "error", "message": "No data received"}), 400

    btn_status = data.get("button")
    machine_id = data.get("machine_id", "Unknown")

    conn = get_db()
    try:
        c = conn.cursor()
        c.execute("""
            INSERT INTO sensor_data (temp, hum, lat, lng, sat, btn, machine_id)
            VALUES (?, ?, ?, ?, ?, ?, ?)
        """, (
            data.get("temp"),
            data.get("hum"),
            data.get("lat"),
            data.get("lng"),
            data.get("sat"),
            btn_status,
            machine_id
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
        print(f">>> 機台 {machine_id} 按下警報！")
        response_data["command"] = "BUZZER_ON"
    else:
        response_data["command"] = "NONE"

    return jsonify(response_data)

# ========== [修改] 串流與讀取資料 (Join 使用者名稱) ==========
# 建立一個共用的 SQL 查詢語句，把 sensor_data 和 machines 表合起來
SELECT_SQL = """
    SELECT s.*, m.username 
    FROM sensor_data s 
    LEFT JOIN machines m ON s.machine_id = m.machine_id 
    ORDER BY s.id DESC LIMIT 50
"""

@app.route('/stream')
def stream():
    # ... (保留 Token 驗證區塊) ...

    def generate():
        last_sent_id = -1
        while True:
            conn = get_db()
            try:
                c = conn.cursor()
                row = c.execute("SELECT MAX(id) as max_id FROM sensor_data").fetchone()
                current_max_id = row["max_id"] if row["max_id"] is not None else 0

                if current_max_id > last_sent_id:
                    # [修改] 使用新的 SQL 語句，包含 username
                    # 注意：因為上面的 SQL 是 DESC，為了圖表我們要轉回 ASC
                    rows = c.execute(f"SELECT * FROM ({SELECT_SQL}) ORDER BY id ASC").fetchall()
                    data_list = [dict(r) for r in rows]

                    json_data = json.dumps(data_list)
                    yield f"data: {json_data}\n\n"
                    
                    last_sent_id = current_max_id
            except Exception as e:
                print(f"Stream Error: {e}")
            finally:
                conn.close()
            time.sleep(1)
            
    return Response(stream_with_context(generate()), mimetype='text/event-stream')

# ========== 讀取資料 API (移除所有 Token 檢查) ==========
@app.route("/api/sensor-data", methods=["GET"])
def get_data():
    # 這裡原本有檢查 Authorization 的程式碼，全部刪掉！
    # 直接開始連線資料庫
    conn = get_db()
    try:
        c = conn.cursor()
        # [修改] 使用新的 SQL
        rows = c.execute(f"SELECT * FROM ({SELECT_SQL}) ORDER BY id ASC").fetchall()
        return jsonify([dict(row) for row in rows])
    except Exception as e:
        return jsonify([]), 500
    finally:
        conn.close()

# ========== [修改] 登入 API (簽發 Token) ==========
@app.route("/auth/login", methods=["POST"])
def login():
    data = request.json
    username = data.get("username")
    password = data.get("password")

    conn = get_db()
    try:
        c = conn.cursor()
        user = c.execute("SELECT * FROM users WHERE username=? AND password=?", (username, password)).fetchone()
        
        if user:
            # [新增] 產生 JWT Token (效期設定 365 天)
            payload = {
                "user": username,
                "role": "admin", # 之後可以做權限管理
                "exp": datetime.datetime.utcnow() + datetime.timedelta(days=365)
            }
            token = jwt.encode(payload, SECRET_KEY, algorithm="HS256")
            
            # 回傳 Token 給前端
            return jsonify({"message": "登入成功", "token": token}),200
        else:
            return jsonify({"message": "帳號或密碼錯誤"}), 401
    finally:
        conn.close()

# ========== 機台註冊 API ==========
@app.route("/api/register-machine", methods=["POST"])
def register_machine():
    data = request.json
    machine_id = data.get("machine_id")
    username = data.get("username")

    if not machine_id or not username:
        return jsonify({"message": "機台號碼與使用者名稱不能空白"}), 400

    conn = get_db()
    try:
        c = conn.cursor()
        # 使用 REPLACE INTO，如果機台號碼重複就更新使用者名稱
        c.execute("REPLACE INTO machines (machine_id, username) VALUES (?, ?)", (machine_id, username))
        conn.commit()
        return jsonify({"message": f"機台 {machine_id} 已綁定使用者 {username}"}), 200
    except Exception as e:
        return jsonify({"message": str(e)}), 500
    finally:
        conn.close()


# ========== 使用者註冊 ==========
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

    # [新增] 建立 machines 資料表
    c.execute("""
        CREATE TABLE IF NOT EXISTS machines (
            machine_id TEXT PRIMARY KEY,
            username TEXT
        )
    """)

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
            machine_id TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        )
    """)
    try:
        c.execute("ALTER TABLE sensor_data ADD COLUMN machine_id TEXT")
    except Exception:
        pass # 欄位可能已存在

    # 建立預設使用者
    has_user = c.execute("SELECT * FROM users").fetchone()
    if not has_user:
        c.execute("INSERT INTO users (username, password) VALUES (?, ?)",
                  ("admin", "1234"))
        print(">>> 已建立預設帳號：admin / 1234")

    conn.commit()
    conn.close()

    # 開啟 threaded=True 支援多執行緒，讓 SSE 不會卡住其他請求
    app.run(host="0.0.0.0", port=5000, threaded=True)