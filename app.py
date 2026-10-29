from datetime import datetime
import os
import socket
from flask import Flask, jsonify, render_template, request

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
app = Flask(__name__, template_folder=os.path.join(BASE_DIR, 'templates'))

# Armazenamento em memória dos dados do GPS
gps_data = {
    "latest": None,
    "history": []
}

def get_local_ip():
    """Descobre o IP da máquina na rede local para facilitar a configuração do ESP8266."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # Não precisa ser alcançável de verdade, serve para o SO selecionar a interface de rede ativa
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

    lat = data.get('lat')
    lng = data.get('lng')

    if lat is None or lng is None:
        return jsonify({"error": "Latitude e Longitude são obrigatórias"}), 400

    entry = {
        "lat": float(lat),
        "lng": float(lng),
        "speed": float(data.get('speed', 0.0)),
        "altitude": float(data.get('altitude', 0.0)),
        "satellites": int(data.get('satellites', 0)),
        "timestamp": data.get('timestamp') or datetime.now().strftime("%d/%m/%Y %H:%M:%S")
    }

    gps_data["latest"] = entry
    gps_data["history"].append(entry)

    # Limita o histórico aos últimos 1000 pontos para poupar memória
    if len(gps_data["history"]) > 1000:
        gps_data["history"].pop(0)

    print(f"[{entry['timestamp']}] Coordenadas recebidas -> Lat: {entry['lat']:.6f}, Lng: {entry['lng']:.6f}, Satélites: {entry['satellites']}")

    return jsonify({"status": "ok", "entry": entry}), 200

@app.route('/api/gps', methods=['GET'])
def get_gps():
    return jsonify(gps_data)

@app.route('/api/gps/clear', methods=['POST'])
def clear_history():
    gps_data["history"] = []
    if gps_data["latest"]:
        gps_data["history"].append(gps_data["latest"])
    return jsonify({"status": "ok", "message": "Histórico limpo"})

if __name__ == '__main__':
    local_ip = get_local_ip()
    print("=" * 60)
    print("  SERVIDOR FLASK GPS NEO-7M / ESP8266 INICIADO")
    print(f"  Acesse o mapa no navegador: http://localhost:5000")
    print(f"  IP para configurar no ESP8266: http://{local_ip}:5000/api/gps")
    print("=" * 60)
    # host='0.0.0.0' permite que outros dispositivos da rede (como o ESP8266) façam requisições
    app.run(host='0.0.0.0', port=5000, debug=True)

