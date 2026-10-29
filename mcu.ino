#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>

// ========================================================
// CONFIGURAÇÃO DO WI-FI E SERVIDOR (VERCEL)
// ========================================================
const char* WIFI_SSID     = "SEU_WIFI_NOME";        // <--- Digite o nome da sua rede Wi-Fi
const char* WIFI_PASSWORD = "SUA_SENHA_WIFI";       // <--- Digite a senha do seu Wi-Fi

// Rota da API na Vercel (com HTTPS)
const char* SERVER_URL    = "https://tiny-gps.vercel.app/api/gps";

// Intervalo de envio quando o GPS JÁ FIXOU a localização (4 segundos)
const unsigned long SEND_INTERVAL_FIXED = 4000;

// Intervalo de heartbeat quando AINDA ESTÁ BUSCANDO satélites (30 segundos)
// Não enviar a cada 3s evita que o processamento pesado do HTTPS bloqueie a porta serial do GPS
const unsigned long HEARTBEAT_NO_FIX    = 30000;

// ========================================================
// CONFIGURAÇÃO DOS PINOS DO GPS NEO-7M
// ========================================================
// Conexão física:
// NEO-7M TX  -> NodeMCU D1 (GPIO 5)
// NEO-7M RX  -> NodeMCU D2 (GPIO 4)
// NEO-7M VCC -> NodeMCU 3V3 / 5V
// NEO-7M GND -> NodeMCU GND
static const int RXPin = 5;
static const int TXPin = 4;
static const uint32_t GPSBaud = 9600;

TinyGPSPlus gps;
SoftwareSerial ss(RXPin, TXPin);

unsigned long lastSendTime = 0;
unsigned long lastSerialPrint = 0;

void feedGps() {
  while (ss.available() > 0) {
    gps.encode(ss.read());
  }
}

void sendStatusUpdate(bool hasFix) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WIFI] Desconectado. Tentando reconectar..."));
    WiFi.reconnect();
    return;
  }

  // Drena a serial antes de iniciar a conexão de rede
  feedGps();

  WiFiClientSecure client;
  client.setInsecure(); // Desativa checagem estrita de certificados para economizar RAM e tempo

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(4000); // Timeout curto de 4s para não travar

  if (http.begin(client, SERVER_URL)) {
    http.addHeader("Content-Type", "application/json");

    String payload = "{";
    payload += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    payload += "\"rssi\":" + String(WiFi.RSSI()) + ",";
    payload += "\"satellites\":" + String(gps.satellites.isValid() ? gps.satellites.value() : 0);

    if (hasFix && gps.location.isValid()) {
      payload += ",\"lat\":" + String(gps.location.lat(), 6);
      payload += ",\"lng\":" + String(gps.location.lng(), 6);
      payload += ",\"speed\":" + String(gps.speed.isValid() ? gps.speed.kmph() : 0.0, 1);
      payload += ",\"altitude\":" + String(gps.altitude.isValid() ? gps.altitude.meters() : 0.0, 1);

      if (gps.date.isValid() && gps.time.isValid()) {
        char dataHora[32];
        sprintf(dataHora, "%02d/%02d/%04d %02d:%02d:%02d",
                gps.date.day(), gps.date.month(), gps.date.year(),
                gps.time.hour(), gps.time.minute(), gps.time.second());
        payload += ",\"timestamp\":\"" + String(dataHora) + "\"";
      }
    } else {
      payload += ",\"gps_fix\":false";
    }
    payload += "}";

    Serial.print(F("[HTTP] Enviando para o servidor -> "));
    Serial.println(payload);

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
      Serial.printf("[HTTP] Codigo de resposta: %d\n", httpCode);
    } else {
      Serial.printf("[HTTP] Falha no POST: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  } else {
    Serial.println(F("[HTTP] Erro ao iniciar conexao HTTPS."));
  }

  // Drena qualquer caractere que tenha chegado durante o POST
  feedGps();
}

void connectWiFi() {
  Serial.println();
  Serial.print(F("[WIFI] Conectando a "));
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    feedGps();
    delay(500);
    Serial.print(F("."));
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println(F("[WIFI] Conectado com sucesso!"));
    Serial.print(F("[WIFI] Endereco IP do ESP8266: "));
    Serial.println(WiFi.localIP());

    // Avisa o servidor uma única vez no boot que o Wi-Fi conectou
    Serial.println(F("[WIFI] Notificando servidor da conexao..."));
    sendStatusUpdate(false);
  } else {
    Serial.println();
    Serial.println(F("[WIFI] Falha ao conectar. Tentara reconectar em segundo plano."));
  }
}

void setup() {
  // Aumenta a velocidade da CPU para 160MHz (dobra a velocidade de cálculo do HTTPS)
  system_update_cpu_freq(160);

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
  // Alimenta os dados seriais do GPS continuamente sem bloqueio
  feedGps();

  // Exibe informações no Serial Monitor a cada 2 segundos (idêntico ao código de teste puro)
  if (millis() - lastSerialPrint >= 2000) {
    lastSerialPrint = millis();

    Serial.println(F("----------------------------------------"));
    Serial.print(F("Satelites em uso: "));
    if (gps.satellites.isValid()) {
      Serial.println(gps.satellites.value());
    } else {
      Serial.println(F("Buscando..."));
    }

    Serial.print(F("Latitude: "));
    if (gps.location.isValid()) {
      Serial.print(gps.location.lat(), 6);
    } else {
      Serial.print(F("Invalido"));
    }

    Serial.print(F(" | Longitude: "));
    if (gps.location.isValid()) {
      Serial.println(gps.location.lng(), 6);
    } else {
      Serial.println(F("Invalido"));
    }

    Serial.print(F("Altitude: "));
    if (gps.altitude.isValid()) {
      Serial.print(gps.altitude.meters());
      Serial.println(F(" m"));
    } else {
      Serial.println(F("Invalido"));
    }

    Serial.print(F("Velocidade: "));
    if (gps.speed.isValid()) {
      Serial.print(gps.speed.kmph());
      Serial.println(F(" km/h"));
    } else {
      Serial.println(F("Invalido"));
    }

    Serial.print(F("Data/Hora (UTC): "));
    if (gps.date.isValid() && gps.time.isValid()) {
      char dataHora[32];
      sprintf(dataHora, "%02d/%02d/%04d %02d:%02d:%02d",
              gps.date.day(), gps.date.month(), gps.date.year(),
              gps.time.hour(), gps.time.minute(), gps.time.second());
      Serial.println(dataHora);
    } else {
      Serial.println(F("Aguardando sincronizacao"));
    }
  }

  // Envio HTTP:
  // - Se já tem fix: envia a cada 4 segundos
  // - Se ainda não tem fix: envia apenas a cada 30 segundos (para não ocupar o processador e travar o GPS)
  unsigned long currentInterval = gps.location.isValid() ? SEND_INTERVAL_FIXED : HEARTBEAT_NO_FIX;
  
  if (millis() - lastSendTime >= currentInterval) {
    lastSendTime = millis();
    sendStatusUpdate(gps.location.isValid());
  }

  // Alerta caso nenhum byte esteja sendo recebido do módulo após 5 segundos
  if (millis() > 5000 && gps.charsProcessed() < 10) {
    Serial.println(F("[ALERTA] Nenhum byte recebido do GPS. Verifique a fiacao e taxa 9600 baud."));
    delay(2000);
  }
}
