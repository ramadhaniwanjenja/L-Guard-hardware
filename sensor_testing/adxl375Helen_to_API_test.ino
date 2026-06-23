/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║   L-GUARD ADXL375 WIRELESS TEST  v3.0                       ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║   ✅ 800 Hz free-running — catches spikes                    ║
 * ║   ✅ Impact ALWAYS interrupts heartbeat retry                 ║
 * ║   ✅ Impact takes priority over heartbeat                     ║
 * ║   ✅ Heartbeat every 10s when no impact                       ║
 * ║   ✅ Peak resets to live after successful upload              ║
 * ║   ✅ Watchdog resets modem after 20 consecutive failures      ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  Wiring:
 *  ADXL375  VCC→3.3V GND→GND SDA→GPIO21 SCL→GPIO22
 *           CS→3.3V (I2C mode)  SDO→GND  (addr 0x53)
 *  SIM A7670E  RXD→4 TXD→5 PWR→15 DTR→18 RESET→19
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL375.h>

// ── MODEM PINS ─────────────────────────────────────────────────
#define MODEM_RXD            5
#define MODEM_TXD            4
#define MODEM_PWR_PIN       15
#define MODEM_DTR_PIN       18
#define MODEM_RESET_PIN     19

// ── SENSOR ────────────────────────────────────────────────────
#define ADXL375_ADDRESS    0x53

// ── NETWORK / DEVICE ───────────────────────────────────────────
const char* APN       = "internet.mtn.rw";
const char* API_URL   = "https://lguard-backend-service.onrender.com/api/v1/telemetry/ingest";
String      DEVICE_ID = "LG-25-0793-002";

// ── THRESHOLDS ────────────────────────────────────────────────
const float ADXL375_LOW_THRESHOLD     =   8.0;
const float ADXL375_MEDIUM_THRESHOLD  =  25.0;
const float ADXL375_HIGH_THRESHOLD    =  50.0;
const float ADXL375_EXTREME_THRESHOLD = 100.0;

// ── TIMING ────────────────────────────────────────────────────
const unsigned long HEARTBEAT_INTERVAL      = 10000UL;
const unsigned long MIN_GAP_BETWEEN_UPLOADS =  2000UL;
const unsigned long STATUS_PRINT            =  1000UL;
const unsigned long PEAK_DECAY_INTERVAL     = 60000UL;
const int           UPLOAD_RETRIES          = 2;
const int           WATCHDOG_THRESHOLD      = 20;
const float         MPS2_TO_G              = 1.0 / 9.80665;

// ── PRIORITY SYNC FLAG ────────────────────────────────────────
bool   thresholdCrossed = false;
bool   pendingImpact    = false;   // remembers impact during heartbeat upload
String triggerReason    = "";

// ── STATE ─────────────────────────────────────────────────────
Adafruit_ADXL375 accel = Adafruit_ADXL375(12345);
HardwareSerial   modemSerial(1);

bool modemReady        = false;
bool gprsConnected     = false;
bool apiSyncInProgress = false;

// Live readings
float liveX = 0, liveY = 0, liveZ = 0, liveTotal = 0;

// Peak readings
float peakX = 0, peakY = 0, peakZ = 0, peakTotal = 0;
unsigned long sampleCount = 0;

// Snapshot at upload time
float lastPeakSent = 0;

// Classification
bool   accidentDetected = false;
String accidentType     = "";
String impactLevel      = "";

// Timing
unsigned long lastUpload    = 0;
unsigned long lastStatus    = 0;
unsigned long lastPeakDecay = 0;

// Stats
unsigned long uploadCount   = 0;
unsigned long uploadSuccess = 0;
int           consecutiveFails = 0;

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

// ══════════════════════════════════════════════════════════════
//  MODEM SETUP
// ══════════════════════════════════════════════════════════════
void setupModem() {
  Serial.println("Initializing SIM A7670E...");

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

  String csq = sendATCommand("AT+CSQ", 1000);
  Serial.print("  . Signal: "); Serial.println(csq);
  Serial.println("✅ Modem ready\n");
}

bool connectGPRS() {
  Serial.println("Connecting to GPRS...");
  if (!modemReady) return false;
  sendATCommand("AT+CGATT=1", 2000);
  sendATCommand("AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 2000);
  Serial.print("  . Activating... ");
  if (sendATCommand("AT+CGACT=1,1", 10000).indexOf("OK") >= 0) {
    if (sendATCommand("AT+CGPADDR=1", 2000).indexOf("+CGPADDR") >= 0) {
      Serial.println("OK\n"); gprsConnected = true; return true;
    }
  }
  Serial.println("FAILED\n"); return false;
}

// ══════════════════════════════════════════════════════════════
//  CLASSIFY IMPACT
// ══════════════════════════════════════════════════════════════
void classifyImpact() {
  if (peakTotal > ADXL375_EXTREME_THRESHOLD) {
    accidentType = "CATASTROPHIC_IMPACT"; impactLevel = "EXTREME"; accidentDetected = true;
  } else if (peakTotal > ADXL375_HIGH_THRESHOLD) {
    accidentType = "SEVERE_IMPACT";       impactLevel = "HIGH";    accidentDetected = true;
  } else if (peakTotal > ADXL375_MEDIUM_THRESHOLD) {
    accidentType = "IMPACT_COLLISION";    impactLevel = "MEDIUM";  accidentDetected = true;
  } else if (peakTotal > ADXL375_LOW_THRESHOLD) {
    accidentType = "LOW_IMPACT";          impactLevel = "LOW";     accidentDetected = false;
  } else {
    accidentType = ""; impactLevel = ""; accidentDetected = false;
  }
}

void resetPeakWindow() {
  peakX = 0;
  peakY = 0;
  peakZ = 0;
  peakTotal = 0;
  sampleCount = 0;

}

void decayPeak() {
  unsigned long now = millis();
  if (now - lastPeakDecay < PEAK_DECAY_INTERVAL) return;
  lastPeakDecay = now;
  if (peakTotal > 0) peakTotal *= 0.5f;
}

// ══════════════════════════════════════════════════════════════
//  JSON BUILDER
// ══════════════════════════════════════════════════════════════
String buildTelemetryJSON() {
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
  json += "\"accidentDetected\":" + String(accidentDetected ? "true" : "false");
  if (accidentDetected || peakTotal > ADXL375_LOW_THRESHOLD) {
    json += ",\"accidentType\":\"" + accidentType + "\",";
    json += "\"impactLevel\":\""   + impactLevel  + "\",";
    json += "\"maxImpact\":"       + String(peakTotal, 1);
  }
  json += "}}";
  return json;
}

// ══════════════════════════════════════════════════════════════
//  HTTP POST
// ══════════════════════════════════════════════════════════════
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
//  WATCHDOG
// ══════════════════════════════════════════════════════════════
void modemWatchdog() {
  if (consecutiveFails < WATCHDOG_THRESHOLD) return;
  Serial.println("\n⚠️  20 consecutive failures — resetting modem...");
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
  Serial.println("✅ Modem watchdog reset complete\n");
}

// ══════════════════════════════════════════════════════════════
//  STATUS PRINT
// ══════════════════════════════════════════════════════════════
void printStatus() {
  Serial.print("│ Live:"); Serial.print(liveTotal, 2);
  Serial.print("g │ CurPk:"); Serial.print(peakTotal, 2);
  Serial.print("g │ LastSent:"); Serial.print(lastPeakSent, 2);
  Serial.print("g │ X:"); Serial.print(liveX, 1);
  Serial.print(" Y:"); Serial.print(liveY, 1);
  Serial.print(" Z:"); Serial.print(liveZ, 1);
  Serial.print(" │ Smpls:"); Serial.print(sampleCount);
  Serial.print(" │ "); Serial.print(accidentType.length() > 0 ? accidentType : "NONE");
  Serial.print(" │ "); Serial.print(uploadSuccess);
  Serial.print("/"); Serial.print(uploadCount);
  Serial.print(" (fails:"); Serial.print(consecutiveFails);
  Serial.println(") │");
  if (lastPeakSent > 16.0)
    Serial.println("  ✅ Peak >16g — NOT clipping like ADXL345");
}

// ══════════════════════════════════════════════════════════════
//  setup()
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n╔══════════════════════════════════════════════════════╗");
  Serial.println("║  L-GUARD ADXL375 WIRELESS TEST v3.0                 ║");
  Serial.println("║  800 Hz · Impact priority · 10s heartbeat · ±200g   ║");
  Serial.println("╚══════════════════════════════════════════════════════╝\n");

  Wire.begin(21, 22);
  Serial.print("ADXL375 (±200g)... ");
  if (!accel.begin(ADXL375_ADDRESS)) {
    Serial.println("FAILED"); while (1) delay(500);
  }
  accel.setDataRate(ADXL343_DATARATE_800_HZ);
  Serial.println("OK — 800 Hz ±200g\n");

  setupModem();
  if (modemReady) {
    if (!connectGPRS()) Serial.println("⚠️  GPRS failed — cannot upload");
  }

  Serial.println("Upload priority:");
  Serial.println("  1st — Impact >8g  (immediate, interrupts heartbeat retry)");
  Serial.println("  2nd — Heartbeat every 10s when no impact");
  Serial.println("\nFree-running peak capture at 800 Hz.");
  Serial.println("Streaming starts in 3s.\n");
  delay(3000);

  lastPeakDecay = millis();
  lastUpload    = millis();
}
// ══════════════════════════════════════════════════════════════
//  loop()
// ══════════════════════════════════════════════════════════════
void loop() {
  // 1. Read sensor — free-running 800 Hz
  sensors_event_t event;
  accel.getEvent(&event);
  liveX     = event.acceleration.x * MPS2_TO_G;
  liveY     = event.acceleration.y * MPS2_TO_G;
  liveZ     = event.acceleration.z * MPS2_TO_G;
  liveTotal = sqrt(liveX*liveX + liveY*liveY + liveZ*liveZ);
  sampleCount++;

  // 2. Update peak
  if (liveTotal > peakTotal) {
    peakTotal = liveTotal;
    peakX = liveX; peakY = liveY; peakZ = liveZ;
  }

  unsigned long now = millis();

  // 3. Detect threshold crossing
if (!thresholdCrossed && liveTotal > ADXL375_LOW_THRESHOLD) {

    thresholdCrossed = true;
    pendingImpact = true;   // keep accident remembered

    if      (peakTotal > ADXL375_EXTREME_THRESHOLD) triggerReason = "EXTREME>100g";
    else if (peakTotal > ADXL375_HIGH_THRESHOLD)    triggerReason = "HIGH>50g";
    else if (peakTotal > ADXL375_MEDIUM_THRESHOLD)  triggerReason = "MEDIUM>25g";
    else                                            triggerReason = "LOW>8g";

    Serial.println("\n⚡ THRESHOLD CROSSED: " + triggerReason +
                   " (" + String(peakTotal, 2) + "g)");
}

  // 4. Upload logic
  bool heartbeatDue = (now - lastUpload >= HEARTBEAT_INTERVAL);
  bool minGapMet    = (now - lastUpload >= MIN_GAP_BETWEEN_UPLOADS);
  bool impactReady  = pendingImpact;

  if (minGapMet && gprsConnected && !apiSyncInProgress &&
      (impactReady || heartbeatDue)) {

    uploadCount++;
    lastPeakSent = peakTotal;
    classifyImpact();
    String payload  = buildTelemetryJSON();
    bool wasImpact = pendingImpact;
    String label    = impactReady ? triggerReason : "HEARTBEAT";

    Serial.print("\n📤 #"); Serial.print(uploadCount);
    Serial.print(" ["); Serial.print(label);
    Serial.print("] peak:"); Serial.print(lastPeakSent, 2);
    Serial.print("g live:"); Serial.print(liveTotal, 2);
    Serial.print("g → ");

    bool ok = false;
    for (int attempt = 1; attempt <= UPLOAD_RETRIES; attempt++) {
      if (httpPOST(payload)) {
        ok = true;
        if (attempt > 1) {
          Serial.print("(retry "); Serial.print(attempt); Serial.print(") ");
        }
        break;
      }

      //Here if a LOWER impact arrives while retrying a heartbeat — abort
  if (!wasImpact && pendingImpact &&
    peakTotal > ADXL375_LOW_THRESHOLD) {

    Serial.print("⚡ IMPACT DURING HEARTBEAT — saved for next upload ");
    break;
}

      if (attempt < UPLOAD_RETRIES) {
        Serial.print("retry... ");
        delay(200);
      }
    }

    if (ok) {
      uploadSuccess++;
      consecutiveFails = 0;
      Serial.print("✅ ("); Serial.print(uploadSuccess);
      Serial.print("/"); Serial.print(uploadCount); Serial.println(")");
    } else {
      consecutiveFails++;
      Serial.print("❌ (streak:"); Serial.print(consecutiveFails);
      Serial.println(")");
    }
// After ANY successful upload,
// clear old peak so it cannot trigger again

if (ok) {
    resetPeakWindow();

    Serial.println("✅ Peak window cleared");

}
// clear flags
thresholdCrossed = false;
pendingImpact = false;
triggerReason = "";
lastUpload = now;
  }
  // 5. Status print every 1s
  if (now - lastStatus >= STATUS_PRINT) {
    printStatus();
    lastStatus = now;
  }
  // 6. Watchdog
  modemWatchdog();
  // 7. Peak safety decay
  decayPeak();
  // NO delay() — 800 Hz free-running
}
