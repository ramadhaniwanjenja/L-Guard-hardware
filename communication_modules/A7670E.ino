#include <HardwareSerial.h>

HardwareSerial A7670E(1);

#define A7670E_TX 4
#define A7670E_RX 5

String apn = "internet.mtn";

String jsonData = R"(
{
  "deviceId": "DEVICE001",
  "latitude": 0.35596,
  "longitude": 32.58252,
  "altitude": 1200,
  "speed": 70,
  "heading": 180,
  "accelerationX": 0.5,
  "accelerationY": 0.2,
  "accelerationZ": 1,
  "gyroX": 0,
  "gyroY": 0,
  "gyroZ": 0,
  "rpm": 0,
  "engineTemp": 0,
  "fuelLevel": 0,
  "batteryLevel": 0,
  "signalStrength": 85,
  "rawData": {}
}
)";

void sendAT(String cmd, uint16_t timeout = 8000) {
  Serial.println(">>> " + cmd);
  A7670E.println(cmd);

  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (A7670E.available()) {
      Serial.write(A7670E.read());
    }
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  A7670E.begin(115200, SERIAL_8N1, A7670E_RX, A7670E_TX);
  delay(1500);

  Serial.println("=== HTTPS POST TO L-GUARD ===");

  sendAT("AT");
  sendAT("AT+CPIN?");
  sendAT("AT+CSQ");
  sendAT("AT+CREG?");
  sendAT("AT+CGATT=1");

  sendAT("AT+CGDCONT=1,\"IP\",\"" + apn + "\"");
  sendAT("AT+CGACT=1,1", 15000);

  // Start HTTP(S)
  sendAT("AT+HTTPINIT", 10000);

  // URL with https
  sendAT("AT+HTTPPARA=\"URL\",\"https://lguard-backend-service.onrender.com/api/v1/telemetry/ingest\"");

  // Content-Type header
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"");

  // Optional: enable SNI on SSL profile 0, ignore if ERROR
  sendAT("AT+CSSLCFG=\"enableSNI\",0,1", 5000);

  // Tell modem how many bytes we’ll send
  int len = jsonData.length();
  sendAT("AT+HTTPDATA=" + String(len) + ",10000", 5000);

  // Now module should reply with "DOWNLOAD"
  // After that, send raw JSON (no extra println at end)
  Serial.println(">>> SENDING JSON BODY...");
  A7670E.print(jsonData);
  Serial.println();
  delay(1000);  // tiny pause

  // Trigger HTTPS POST
  sendAT("AT+HTTPACTION=1", 30000);   // 1 = POST

  // Wait then read response
  delay(2000);
  sendAT("AT+HTTPREAD=0,512", 20000);

  // End HTTP(S)
  sendAT("AT+HTTPTERM", 10000);

  Serial.println("=== DONE ===");
}

void loop() {}