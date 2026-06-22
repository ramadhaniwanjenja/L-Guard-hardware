/*
 * ADXL375 STANDALONE TEST + REAL-TIME CELLULAR UPLOAD
 * L-Guard High-G Peak Capture — sends every 500ms via SIM A7670E
 *
 * Wiring ESP32:
 *   ADXL375  VCC→3.3V  GND→GND  SDA→GPIO21  SCL→GPIO22
 *            CS→3.3V  SDO→GND  (addr 0x53)
 *   SIM A7670E  RXD→GPIO5  TXD→GPIO4  PWR→GPIO15  DTR→GPIO18  RESET→GPIO19
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL375.h>

// ── MODEM PINS ─────────────────────────────────────────────────
#define MODEM_RXD       5
#define MODEM_TXD       4
#define MODEM_PWR_PIN  15
#define MODEM_DTR_PIN  18
#define MODEM_RESET_PIN 19

// ── SENSOR ────────────────────────────────────────────────────
#define ADXL375_ADDRESS 0x53

// ── NETWORK / DEVICE ───────────────────────────────────────────
const char* APN       = "internet.mtn.rw";
const char* API_URL   = "https://lguard-backend-service.onrender.com/api/v1/telemetry/ingest";
String      DEVICE_ID = "LG-25-0793-002";

// ── THRESHOLDS ────────────────────────────────────────────────
const float THRESH_LOW     =   8.0;
const float THRESH_MEDIUM  =  25.0;
const float THRESH_HIGH    =  50.0;
const float THRESH_EXTREME = 100.0;

// ── TIMING ────────────────────────────────────────────────────
const unsigned long PRINT_AND_UPLOAD_INTERVAL = 500UL;
const float         MPS2_TO_G                 = 1.0 / 9.80665;
const int           WATCHDOG_THRESHOLD        = 20;

// ── SENSOR STATE ──────────────────────────────────────────────
Adafruit_ADXL375 accel = Adafruit_ADXL375(12345);

float currentX = 0, currentY = 0, currentZ = 0, currentTotal = 0;
float peakX    = 0, peakY    = 0, peakZ    = 0, peakTotal    = 0;
unsigned long sampleCount = 0;
unsigned long lastPrint   = 0;

// ── MODEM STATE ───────────────────────────────────────────────
HardwareSerial modemSerial(1);
bool modemReady        = false;
bool gprsConnected     = false;
bool apiSyncInProgress = false;

// ── UPLOAD STATS ──────────────────────────────────────────────
unsigned long uploadCount      = 0;
unsigned long uploadSuccess    = 0;
int           consecutiveFails = 0;

// ══════════════════════════════════════════════════════════════
//  HELPERS
// ══════════════════════════════════════════════════════════════
String classifyImpact(float g) {
  if (g > THRESH_EXTREME) return "EXTREME>100g";
  if (g > THRESH_HIGH)    return "HIGH>50g";
  if (g > THRESH_MEDIUM)  return "MEDIUM>25g";
  if (g > THRESH_LOW)     return "LOW>8g";
  return "NONE";
}

String bar(float g, float maxG = 100.0, int width = 30) {
  int filled = constrain((int)((g / maxG) * width), 0, width);
  String b = "[";
  for (int i = 0; i < width; i++) b += (i < filled) ? "#" : "-";
  b += "]";
  return b;
}

void resetPeak() {
  peakX = currentX;
  peakY = currentY;
  peakZ = currentZ;
  peakTotal = currentTotal;
  sampleCount = 0;
}

// ══════════════════════════════════════════════════════════════
//  MODEM HELPERS
// ══════════════════════════════════════════════════════════════
String readModemResponse(unsigned long timeout = 2000) {
  String resp = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (modemSerial.available()) { char c = modemSerial.read(); resp += c; }
    if (resp.indexOf("OK\r\n")       >= 0 ||
        resp.indexOf("ERROR\r\n")    >= 0 ||
        resp.indexOf("DOWNLOAD")     >= 0 ||
        resp.indexOf("+HTTPACTION:") >= 0) break;
  }
  resp.trim();
  return resp;
}

String sendATCommand(String cmd, unsigned long timeout = 1000) {
  modemSerial.println(cmd);
  return readModemResponse(timeout);
}

void setupModem() {
  Serial.println("\nInitializing SIM A7670E...");
  pinMode(MODEM_PWR_PIN,   OUTPUT);
  pinMode(MODEM_DTR_PIN,   OUTPUT);
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_PWR_PIN,   HIGH);
  digitalWrite(MODEM_DTR_PIN,   HIGH);
  digitalWrite(MODEM_RESET_PIN, LOW);

  modemSerial.begin(115200, SERIAL_8N1, MODEM_RXD, MODEM_TXD);
  delay(2000);
  while (modemSerial.available()) modemSerial.read();

  Serial.println("  . Powering on...");
  digitalWrite(MODEM_PWR_PIN, LOW);  delay(100);
  digitalWrite(MODEM_PWR_PIN, HIGH); delay(1500);
  digitalWrite(MODEM_PWR_PIN, LOW);  delay(5000);

  sendATCommand("AT",   500);
  sendATCommand("AT",   500);
  sendATCommand("ATE0", 500);

  Serial.print("  . SIM card... ");
  if (sendATCommand("AT+CPIN?", 2000).indexOf("READY") < 0) {
    Serial.println("FAILED"); modemReady = false; return;
  }
  Serial.println("OK");

  Serial.print("  . Network... ");
  for (int i = 0; i < 15; i++) {
    String nr = sendATCommand("AT+CREG?", 2000);
    if (nr.indexOf("+CREG: 0,1") >= 0 || nr.indexOf("+CREG: 0,5") >= 0) {
      Serial.println("OK"); modemReady = true; break;
    }
    delay(2000);
  }
  if (!modemReady) { Serial.println("FAILED"); return; }
  Serial.println("✅ Modem ready");
}

bool connectGPRS() {
  Serial.println("Connecting GPRS...");
  if (!modemReady) return false;
  sendATCommand("AT+CGATT=1", 2000);
  sendATCommand("AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 2000);
  Serial.print("  . Activating... ");
  if (sendATCommand("AT+CGACT=1,1", 10000).indexOf("OK") >= 0) {
    if (sendATCommand("AT+CGPADDR=1", 2000).indexOf("+CGPADDR") >= 0) {
      Serial.println("OK"); gprsConnected = true; return true;
    }
  }
  Serial.println("FAILED"); return false;
}

void modemWatchdog() {
  if (consecutiveFails < WATCHDOG_THRESHOLD) return;
  Serial.println("\n⚠️  Watchdog — resetting modem...");
  sendATCommand("AT+HTTPTERM", 1000);
  digitalWrite(MODEM_RESET_PIN, HIGH); delay(500);
  digitalWrite(MODEM_RESET_PIN, LOW);  delay(8000);
  modemReady = false; gprsConnected = false;
  for (int i = 0; i < 10; i++) {
    String r = sendATCommand("AT+CREG?", 2000);
    if (r.indexOf("+CREG: 0,1") >= 0 || r.indexOf("+CREG: 0,5") >= 0) {
      modemReady = true; break;
    }
    delay(2000);
  }
  if (modemReady) connectGPRS();
  consecutiveFails = 0;
  Serial.println("✅ Watchdog reset complete");
}

// ══════════════════════════════════════════════════════════════
//  JSON + HTTP POST
// ══════════════════════════════════════════════════════════════
String buildJSON() {
  String level = classifyImpact(peakTotal);
  bool   isAcc = (peakTotal > THRESH_MEDIUM);

  String json = "{";
  json += "\"deviceId\":\"" + DEVICE_ID + "\",";
  json += "\"latitude\":0,\"longitude\":0,\"altitude\":0,";
  json += "\"speed\":0,\"heading\":0,";
  json += "\"accelerationX\":" + String(peakX, 2) + ",";
  json += "\"accelerationY\":" + String(peakY, 2) + ",";
  json += "\"accelerationZ\":" + String(peakZ, 2) + ",";
  json += "\"gyroX\":0,\"gyroY\":0,\"gyroZ\":0,";
  json += "\"rpm\":0,\"engineTemp\":0,\"fuelLevel\":0,";
  json += "\"batteryLevel\":0,\"signalStrength\":85,";
  json += "\"rawData\":{";
  json += "\"totalAccel\":"  + String(peakTotal, 2) + ",";
  json += "\"totalGyro\":0,\"pitchAngle\":0,\"rollAngle\":0,";
  json += "\"vibration\":false,\"satellites\":0,\"hdop\":0,";
  json += "\"fixQuality\":\"TEST_MODE\",";
  json += "\"accidentDetected\":" + String(isAcc ? "true" : "false");
  if (peakTotal > THRESH_LOW) {
    json += ",\"accidentType\":\"" + level + "\",";
    json += "\"impactLevel\":\"" + level + "\",";
    json += "\"maxImpact\":"     + String(peakTotal, 1);
  }
  json += "}}";
  return json;
}

bool httpPOST(String jsonData) {
  if (!gprsConnected || apiSyncInProgress) return false;
  apiSyncInProgress = true;

  bool success = false;
  sendATCommand("AT+HTTPTERM", 800);
  delay(200);
  sendATCommand("AT+CSSLCFG=\"enableSNI\",0,1", 1500);
  sendATCommand("AT+HTTPINIT",                  1500);
  modemSerial.println("AT+HTTPPARA=\"URL\",\"" + String(API_URL) + "\"");
  delay(500); readModemResponse(1500);
  modemSerial.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
  delay(500); readModemResponse(1500);
  modemSerial.println("AT+HTTPDATA=" + String(jsonData.length()) + ",5000");
  delay(500);
  String dlResp = readModemResponse(2000);

  if (dlResp.indexOf("DOWNLOAD") >= 0) {
    modemSerial.print(jsonData);
    delay(1500);
    if (readModemResponse(2000).indexOf("OK") >= 0) {
      modemSerial.println("AT+HTTPACTION=1");
      for (int i = 0; i < 12; i++) {
        delay(500);
        if (modemSerial.available()) {
          String r = readModemResponse(1500);
          if (r.indexOf("+HTTPACTION") >= 0) {
            success = (r.indexOf(",200,") >= 0 || r.indexOf(",201,") >= 0);
            break;
          }
        }
      }
    }
  }
  sendATCommand("AT+HTTPTERM", 800);
  apiSyncInProgress = false;
  return success;
}

// ══════════════════════════════════════════════════════════════
//  setup()
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n============================================");
  Serial.println("   ADXL375 REAL-TIME UPLOAD TEST");
  Serial.println("   800 Hz capture · 500ms send · ±200g");
  Serial.println("============================================");

  Wire.begin(21, 22);
  Serial.print("ADXL375 ... ");
  if (!accel.begin(ADXL375_ADDRESS)) {
    Serial.println("FAILED — check wiring");
    while (1) delay(500);
  }
  accel.setDataRate(ADXL343_DATARATE_800_HZ);
  Serial.println("OK — 800 Hz ±200g\n");

  setupModem();
  if (modemReady) connectGPRS();

  Serial.println("\nStreaming starts now.");
  Serial.println("Peak captures between 500ms windows.");
  Serial.println("Peak resets to current live after each successful upload.\n");

  lastPrint = millis();
}

// ══════════════════════════════════════════════════════════════
//  loop()
// ══════════════════════════════════════════════════════════════
void loop() {
  // 1. Read sensor — free-running 800 Hz
  sensors_event_t event;
  accel.getEvent(&event);

  currentX     = event.acceleration.x * MPS2_TO_G;
  currentY     = event.acceleration.y * MPS2_TO_G;
  currentZ     = event.acceleration.z * MPS2_TO_G;
  currentTotal = sqrt(currentX*currentX + currentY*currentY + currentZ*currentZ);

  sampleCount++;

  // 2. Update peak
  if (currentTotal > peakTotal) {
    peakTotal = currentTotal;
    peakX     = currentX;
    peakY     = currentY;
    peakZ     = currentZ;
  }

  unsigned long now = millis();

  // 3. Every 500ms — print + upload
  if (now - lastPrint >= PRINT_AND_UPLOAD_INTERVAL) {
    lastPrint = now;

    // Print (same style as standalone test)
    Serial.println("--------------------------------------------------");
    Serial.print("Current X:"); Serial.print(currentX, 2);
    Serial.print("g Y:");       Serial.print(currentY, 2);
    Serial.print("g Z:");       Serial.print(currentZ, 2);
    Serial.print("g | Total:"); Serial.print(currentTotal, 2); Serial.println("g");

    Serial.print("PEAK    X:"); Serial.print(peakX, 2);
    Serial.print("g Y:");       Serial.print(peakY, 2);
    Serial.print("g Z:");       Serial.print(peakZ, 2);
    Serial.print("g | Peak:"); Serial.print(peakTotal, 2); Serial.println("g");

    Serial.print("Level: "); Serial.println(classifyImpact(peakTotal));
    Serial.print("Bar: ");
    Serial.print(bar(peakTotal));
    Serial.print(" "); Serial.print(peakTotal, 1); Serial.println("g");
    Serial.print("Samples in window: "); Serial.println(sampleCount);

    if (peakTotal > 16.0)
      Serial.println("✅ Peak >16g — NOT behaving like ADXL345");

    // Upload
    if (gprsConnected && !apiSyncInProgress) {
      uploadCount++;
      Serial.print("📤 #"); Serial.print(uploadCount);
      Serial.print(" → ");

      if (httpPOST(buildJSON())) {
        uploadSuccess++;
        consecutiveFails = 0;
        Serial.print("✅ (");
        Serial.print(uploadSuccess); Serial.print("/");
        Serial.print(uploadCount); Serial.println(")");
        // Reset peak to current live — fresh window
        resetPeak();
      } else {
        consecutiveFails++;
        Serial.print("❌ streak:"); Serial.print(consecutiveFails);
        Serial.println(" — peak kept");
      }
    } else {
      Serial.println("⚠️  No GPRS — skipping upload");
      resetPeak();   // still reset for display purposes
    }
  }

  // 4. Watchdog
  modemWatchdog();

  // NO delay() — 800 Hz free-running
}
