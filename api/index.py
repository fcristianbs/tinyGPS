from datetime import datetime
import os
import socket
import time
from urllib.parse import parse_qs
from dotenv import load_dotenv
from flask import Flask, jsonify, render_template, request
import pymysql

# Carrega variáveis de ambiente do .env
load_dotenv()

# Configuração robusta do diretório de templates (funciona local e na Vercel)
current_dir = os.path.dirname(os.path.abspath(__file__))
root_dir = os.path.dirname(current_dir)

template_dirs = [
    os.path.join(current_dir, 'templates'),
    os.path.join(root_dir, 'templates'),
    'templates'
]

template_folder = current_dir
for tdir in template_dirs:
    if os.path.isdir(tdir):
        template_folder = tdir
        break

app = Flask(__name__, template_folder=template_folder)

# Middleware para a Vercel: restaura o caminho real a partir do parâmetro path da reescrita
class VercelPathMiddleware:
    def __init__(self, wsgi_app):
        self.wsgi_app = wsgi_app

    def __call__(self, environ, start_response):
        query_string = environ.get('QUERY_STRING', '')
        if query_string:
            qs = parse_qs(query_string)
            if 'path' in qs and qs['path']:
                real_path = qs['path'][0]
                if not real_path.startswith('/'):
                    real_path = '/' + real_path
                environ['PATH_INFO'] = real_path

        # Caso a Vercel chame /api/index diretamente sem query string, cai na home
        if environ.get('PATH_INFO') == '/api/index':
            environ['PATH_INFO'] = '/'

        return self.wsgi_app(environ, start_response)

app.wsgi_app = VercelPathMiddleware(app.wsgi_app)

# ========================================================
# CONEXÃO COM O BANCO DE DADOS (TABELA DISCRETA: geo_telemetry_logs)
# ========================================================
def get_db_connection():
    host = os.getenv('DB_HOST')
    if not host:
        return None
    try:
        return pymysql.connect(
            host=host,
            port=int(os.getenv('DB_PORT', 3306)),
            user=os.getenv('DB_USER'),
            password=os.getenv('DB_PASSWORD'),
            database=os.getenv('DB_NAME'),
            connect_timeout=4,
            autocommit=True,
            cursorclass=pymysql.cursors.DictCursor
        )
    except Exception as e:
        print(f"[DB] Erro de conexao com banco MySQL: {e}")
        return None

def init_db():
    conn = get_db_connection()
    if not conn:
        return
    try:
        with conn.cursor() as cur:
            cur.execute("""
            CREATE TABLE IF NOT EXISTS geo_telemetry_logs (
                id BIGINT AUTO_INCREMENT PRIMARY KEY,
                device_ip VARCHAR(45) NULL,
                rssi INT NULL,
                satellites INT DEFAULT 0,
                latitude DECIMAL(10, 7) NOT NULL,
                longitude DECIMAL(10, 7) NOT NULL,
                altitude DECIMAL(8, 2) DEFAULT 0.00,
                speed_kmph DECIMAL(6, 2) DEFAULT 0.00,
                gps_time VARCHAR(30) NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                INDEX idx_created_at (created_at)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
            """)
    except Exception as e:
        print(f"[DB] Erro ao inicializar tabela: {e}")
    finally:
        conn.close()

def save_point_to_db(entry, dev):
    conn = get_db_connection()
    if not conn:
        return
    try:
        with conn.cursor() as cur:
            cur.execute("""
                INSERT INTO geo_telemetry_logs 
                (device_ip, rssi, satellites, latitude, longitude, altitude, speed_kmph, gps_time)
                VALUES (%s, %s, %s, %s, %s, %s, %s, %s)
            """, (
                dev.get('ip'),
                dev.get('rssi'),
                entry.get('satellites', 0),
                entry['lat'],
                entry['lng'],
                entry.get('altitude', 0.0),
                entry.get('speed', 0.0),
                entry.get('timestamp')
            ))
    except Exception as e:
        print(f"[DB] Erro ao salvar ponto no banco: {e}")
    finally:
        conn.close()

def load_recent_points_from_db(limit=300):
    conn = get_db_connection()
    if not conn:
        return []
    try:
        with conn.cursor() as cur:
            cur.execute("""
                SELECT latitude as lat, longitude as lng, speed_kmph as speed, 
                       altitude, satellites, gps_time as timestamp, created_at
                FROM geo_telemetry_logs
                ORDER BY id DESC
                LIMIT %s
            """, (limit,))
            rows = cur.fetchall()
            rows.reverse()
            for r in rows:
                r['lat'] = float(r['lat'])
                r['lng'] = float(r['lng'])
                r['speed'] = float(r['speed'] or 0.0)
                r['altitude'] = float(r['altitude'] or 0.0)
                if not r['timestamp'] and r['created_at']:
                    r['timestamp'] = r['created_at'].strftime("%d/%m/%Y %H:%M:%S")
                r.pop('created_at', None)
            return rows
    except Exception as e:
        print(f"[DB] Erro ao carregar historico do banco: {e}")
        return []
    finally:
        conn.close()

# Inicializa tabela se o banco estiver configurado
try:
    init_db()
except Exception:
    pass

# Armazenamento em memória dos dados do GPS e status do ESP8266
gps_data = {
    "latest": None,
    "history": [],
    "device": {
        "online": False,
        "last_seen": None,
        "last_seen_epoch": 0,
        "ip": None,
        "rssi": None,
        "gps_fix": False,
        "satellites": 0
    }
}

def get_local_ip():
    """Descobre o IP da máquina na rede local para facilitar a configuração do ESP8266."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = '127.0.0.1'
    finally:
        s.close()
    return ip

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/gps', methods=['POST'])
def receive_gps():
    data = request.get_json(silent=True)
    if not data:
        return jsonify({"error": "Formato JSON inválido"}), 400

    now_epoch = time.time()
    now_str = datetime.now().strftime("%d/%m/%Y %H:%M:%S")

    # Atualiza o status de conexão da placa ESP8266
    gps_data["device"]["online"] = True
    gps_data["device"]["last_seen"] = now_str
    gps_data["device"]["last_seen_epoch"] = now_epoch
    gps_data["device"]["ip"] = data.get("ip")
    gps_data["device"]["rssi"] = data.get("rssi")
    gps_data["device"]["satellites"] = int(data.get("satellites", 0))

    lat = data.get('lat')
    lng = data.get('lng')

    # Se a placa enviou dados mas ainda está sem sinal de GPS (apenas Wi-Fi conectado)
    if lat is None or lng is None:
        gps_data["device"]["gps_fix"] = False
        print(f"[{now_str}] Placa conectada ao Wi-Fi! IP: {data.get('ip')} | Satélites: {gps_data['device']['satellites']} (Aguardando fix do GPS)")
        return jsonify({
            "status": "ok",
            "message": "Placa online via Wi-Fi. Aguardando satélites...",
            "device": gps_data["device"]
        }), 200

    # Quando as coordenadas forem válidas
    gps_data["device"]["gps_fix"] = True
    entry = {
        "lat": float(lat),
        "lng": float(lng),
        "speed": float(data.get('speed', 0.0)),
        "altitude": float(data.get('altitude', 0.0)),
        "satellites": int(data.get('satellites', 0)),
        "timestamp": data.get('timestamp') or now_str
    }

    gps_data["latest"] = entry
    gps_data["history"].append(entry)

    if len(gps_data["history"]) > 1000:
        gps_data["history"].pop(0)

    # Persiste na tabela discreta do banco MySQL
    save_point_to_db(entry, gps_data["device"])

    print(f"[{entry['timestamp']}] Coordenadas salvas no BD -> Lat: {entry['lat']:.6f}, Lng: {entry['lng']:.6f}, Sat: {entry['satellites']}")

    return jsonify({"status": "ok", "entry": entry, "device": gps_data["device"]}), 200

@app.route('/api/gps', methods=['GET'])
def get_gps():
    # Se o histórico em memória estiver vazio (ex: inicialização ou reinício na Vercel), restaura do BD
    if not gps_data["history"]:
        db_points = load_recent_points_from_db(limit=300)
        if db_points:
            gps_data["history"] = db_points
            if not gps_data["latest"]:
                gps_data["latest"] = db_points[-1]

    # Se não houver envio da placa há mais de 45 segundos, marca como offline
    if gps_data["device"]["last_seen_epoch"] > 0:
        if time.time() - gps_data["device"]["last_seen_epoch"] > 45:
            gps_data["device"]["online"] = False

    return jsonify(gps_data)

@app.route('/api/gps/clear', methods=['POST'])
def clear_history():
    gps_data["history"] = []
    if gps_data["latest"]:
        gps_data["history"].append(gps_data["latest"])
    return jsonify({"status": "ok", "message": "Histórico em tela limpo"})

if __name__ == '__main__':
    local_ip = get_local_ip()
    print("=" * 60)
    print("  SERVIDOR FLASK GPS NEO-7M / ESP8266 INICIADO")
    print(f"  Acesse o mapa no navegador: http://localhost:5000")
    print(f"  IP para configurar no ESP8266: http://{local_ip}:5000/api/gps")
    print("=" * 60)
    app.run(host='0.0.0.0', port=5000, debug=True)
