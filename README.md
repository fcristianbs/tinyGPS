# TinyGPS - ESP8266 (NEO-7M) & Flask Tracker

Rastreador GPS em tempo real utilizando módulo **NEO-7M**, microcontrolador **ESP8266 (NodeMCU)** e servidor web em **Python (Flask + Leaflet.js)**.

## 🛰️ Visão Geral

- **Microcontrolador**: ESP8266 (NodeMCU) conectado via Serial ao módulo GPS NEO-7M.
- **Transmissão**: O ESP8266 se conecta ao Wi-Fi e envia coordenadas via HTTP POST para a API Flask.
- **Interface**: Mapa interativo em tempo real via OpenStreetMap (Leaflet.js) com rota percorrida e telemetria (velocidade, altitude, satélites).
- **Hospedagem**: Pronto para rodar localmente ou em plataformas em nuvem (Vercel, Render, etc.).

---

## 📂 Estrutura do Repositório

```
tinyGPS/
├── api/
│   └── index.py            # Entry point Serverless para Vercel
├── templates/
│   └── index.html          # Interface do mapa com Leaflet.js
├── app.py                  # Servidor Flask e rotas da API
├── mcu.ino                 # Firmware Arduino/ESP8266
├── requirements.txt        # Dependências Python
├── vercel.json             # Configuração de rotas no Vercel
└── README.md
```

---

## 🚀 Como Rodar Localmente

1. Instale as dependências:
   ```bash
   pip install -r requirements.txt
   ```
2. Inicie o servidor:
   ```bash
   py app.py
   ```
3. Abra no navegador: [http://localhost:5000](http://localhost:5000)

---

## ⚡ Firmware ESP8266 (Arduino IDE)

1. Abra o arquivo `mcu.ino` na **Arduino IDE**.
2. Configure seu Wi-Fi e o endpoint do servidor:
   ```cpp
   const char* WIFI_SSID     = "SEU_WIFI";
   const char* WIFI_PASSWORD = "SUA_SENHA";
   const char* SERVER_URL    = "http://SEU_IP_OU_DOMINIO/api/gps";
   ```
3. Conexões de pinos (NEO-7M -> NodeMCU ESP8266):
   - **VCC** -> 3V3 / 5V
   - **GND** -> GND
   - **TX**  -> D1 (GPIO 5)
   - **RX**  -> D2 (GPIO 4)
4. Faça o upload para a placa NodeMCU.

---

## ☁️ Deploy no Vercel

1. Importe este repositório no seu painel da [Vercel](https://vercel.com).
2. O arquivo `vercel.json` e a pasta `api/` já estão configurados automaticamente para a execução Serverless de aplicações Python.
3. Obtenha a URL gerada (ex: `https://tiny-gps-fcristianbs.vercel.app`) e aponte o `SERVER_URL` do `mcu.ino` para:
   ```
   https://tiny-gps-fcristianbs.vercel.app/api/gps
   ```

> ⚠️ **Nota sobre Vercel e Serverless:**  
> Como a Vercel utiliza funções Serverless sem estado em memória persistente, para armazenar o histórico de coordenadas em longo prazo na nuvem recomenda-se conectar um banco de dados leve ou Redis (ex: Upstash Redis ou Supabase). Para testes contínuos sem banco de dados, plataformas com instâncias persistentes como [Render.com](https://render.com) ou servidores VPS também são excelentes opções.
