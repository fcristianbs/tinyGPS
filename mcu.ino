#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>

// ========================================================
// CONFIGURAÇÃO DO WI-FI E SERVIDOR FLASK
// ========================================================
const char* WIFI_SSID     = "SEU_WIFI_NOME";        // <--- Digite o nome da sua rede Wi-Fi
const char* WIFI_PASSWORD = "SUA_SENHA_WIFI";       // <--- Digite a senha do seu Wi-Fi

// Endereço IP do seu computador na rede local (exibido no terminal ao iniciar o app.py)
// Exemplo: "http://192.168.1.100:5000/api/gps"
const char* SERVER_URL    = "http://SEU_IP_DO_PC:5000/api/gps";

// Intervalo de envio das coordenadas via HTTP (em milissegundos)
const unsigned long SEND_INTERVAL = 3000; // Envia a cada 3 segundos

// ========================================================
// CONFIGURAÇÃO DOS PINOS DO GPS NEO-7M
// ========================================================
// Pinos: RX (D1 = GPIO 5), TX (D2 = GPIO 4)
// Conexão física:
// NEO-7M TX  -> NodeMCU D1 (GPIO 5)
// NEO-7M RX  -> NodeMCU D2 (GPIO 4)
// NEO-7M VCC -> NodeMCU 3V3 (ou 5V se o módulo tiver regulador)
// NEO-7M GND -> NodeMCU GND
static const int RXPin = 5;
static const int TXPin = 4;
static const uint32_t GPSBaud = 9600;

TinyGPSPlus gps;
SoftwareSerial ss(RXPin, TXPin);

unsigned long lastSendTime = 0;

void connectWiFi() {
  Serial.println();
  Serial.print(F("[WIFI] Conectando a "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(F("."));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println(F("[WIFI] Conectado com sucesso!"));
    Serial.print(F("[WIFI] Endereco IP do ESP8266: "));
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println(F("[WIFI] Falha ao conectar. Tentara reconectar no loop."));
  }
}

void sendGpsData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WIFI] Desconectado. Tentando reconectar..."));
    WiFi.reconnect();
    return;
  }

  WiFiClient client;
  HTTPClient http;

  if (http.begin(client, SERVER_URL)) {
    http.addHeader("Content-Type", "application/json");

    // Monta o JSON com os dados do GPS
    String payload = "{";
    payload += "\"lat\":" + String(gps.location.lat(), 6) + ",";
    payload += "\"lng\":" + String(gps.location.lng(), 6) + ",";
    payload += "\"speed\":" + String(gps.speed.isValid() ? gps.speed.kmph() : 0.0, 1) + ",";
    payload += "\"altitude\":" + String(gps.altitude.isValid() ? gps.altitude.meters() : 0.0, 1) + ",";
    payload += "\"satellites\":" + String(gps.satellites.isValid() ? gps.satellites.value() : 0) + ",";

    if (gps.date.isValid() && gps.time.isValid()) {
      char dataHora[32];
      sprintf(dataHora, "%02d/%02d/%04d %02d:%02d:%02d",
              gps.date.day(), gps.date.month(), gps.date.year(),
              gps.time.hour(), gps.time.minute(), gps.time.second());
      payload += "\"timestamp\":\"" + String(dataHora) + "\"";
    } else {
      payload += "\"timestamp\":\"\"";
    }
    payload += "}";

    Serial.print(F("[HTTP] Enviando coordenadas -> "));
    Serial.println(payload);

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
      Serial.printf("[HTTP] Codigo de resposta: %d\n", httpCode);
    } else {
      Serial.printf("[HTTP] Falha no POST: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.println(F("[HTTP] Erro ao iniciar conexao HTTP."));
  }
}

void setup() {
  Serial.begin(115200);
  ss.begin(GPSBaud);

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("  NEO-7M GPS + ESP8266 -> Servidor Web   "));
  Serial.println(F("========================================"));

  connectWiFi();

  Serial.println(F("[GPS] Aguardando sinal de satelites..."));
}

void loop() {
  // Alimenta os dados seriais do GPS continuamente
  while (ss.available() > 0) {
    gps.encode(ss.read());
  }

  // A cada SEND_INTERVAL milissegundos, processa e envia os dados
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = millis();

    Serial.println(F("----------------------------------------"));
    Serial.print(F("Satelites detectados: "));
    if (gps.satellites.isValid()) {
      Serial.println(gps.satellites.value());
    } else {
      Serial.println(F("Buscando..."));
    }

    if (gps.location.isValid()) {
      Serial.print(F("Localizacao FIXA: Lat="));
      Serial.print(gps.location.lat(), 6);
      Serial.print(F(", Lng="));
      Serial.println(gps.location.lng(), 6);

      // Envia os dados para a API Flask
      sendGpsData();
    } else {
      Serial.println(F("[GPS] Aguardando fix de localizacao dos satelites (leve o modulo para perto da janela/ar livre)..."));
    }
  }

  // Alerta caso nenhum byte esteja sendo recebido do módulo após 5 segundos
  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println(F("[ALERTA] Nenhum byte recebido do GPS. Verifique a fiacao (TX/RX invertidos?) e taxa 9600 baud."));
    delay(5000);
  }
}
