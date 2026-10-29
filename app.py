from api.index import app, get_local_ip

if __name__ == '__main__':
    local_ip = get_local_ip()
    print("=" * 60)
    print("  SERVIDOR FLASK GPS NEO-7M / ESP8266 INICIADO")
    print(f"  Acesse o mapa no navegador: http://localhost:5000")
    print(f"  IP para configurar no ESP8266: http://{local_ip}:5000/api/gps")
    print("=" * 60)
    app.run(host='0.0.0.0', port=5000, debug=True)
