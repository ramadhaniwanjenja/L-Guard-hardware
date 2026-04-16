/*
 * ╔══════════════════════════════════════════════════════════════╗
 * ║   L-GUARD VEHICLE SAFETY SYSTEM  v6.1                       ║
 * ║   ADXL375 (±200g) + MPU6050 + GPS + GSM                     ║
 * ╠══════════════════════════════════════════════════════════════╣
 * ║  NEW IN v6.1 vs v6.0:                                        ║
 * ║  ✅ PRIORITY API SYNC on sensor band-change                   ║
 * ║       Impact:   NONE→LOW→MEDIUM→HIGH→EXTREME                 ║
 * ║       Tilt:     FLAT→WARN(45°)→ROLLOVER(70°)                 ║
 * ║       Speed:    STOPPED→CRAWL→NORMAL→FAST→OVERSPEED          ║
 * ║       Gyro:     STILL→MOVING→SPINNING                        ║
 * ║  ✅ syncReason tag injected into every telemetry JSON packet  ║
 * ║  ✅ Peak tracking: g, gyro, roll, pitch, speed               ║
 * ║  ✅ Peaks printed live in Serial Monitor compact status       ║
 * ║  ✅ Peaks included in telemetry + accident JSON payloads      ║
 * ║  ✅ Rollover fix: needs sustained tilt AND min impact         ║
 * ║       → prevents false alarm while parked on a slope         ║
 * ║  ✅ 60-second gentle peak decay (50% every cycle)            ║
 * ║  ✅ Accident Serial print shows both live and peak values     ║
 * ║  ✅ buildAccidentJSON includes accidentType, peakImpactG etc  ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 *  WIRING (unchanged from v6.0):
 *    ADXL375  SDA→GPIO21  SCL→GPIO22  SDO→GND (addr 0x53)
 *    MPU6050  SDA→GPIO21  SCL→GPIO22  AD0→VCC (addr 0x69)
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL375.h>
#include <Adafruit_MPU6050.h>

// ── PIN DEFINITIONS ────────────────────────────────────────────
#define MODEM_RI_PIN        2
#define MODEM_RXD           5
#define MODEM_TXD           4
#define MODEM_PWR_PIN       15
#define MODEM_DTR_PIN       18
#define MODEM_RESET_PIN     19
#define GPS_MODULE_TX       16
#define GPS_MODULE_RX       17
#define BUZZER_PIN          25
#define OK_BUTTON_PIN       26
#define MENU_BUTTON_PIN     27
#define BIKE_MONITOR_PIN    33
#define BATTERY_ADC_PIN     34
#define SAFE_LED_PIN        12
#define DANGER_LED_PIN      14
#define GPS_LED_PIN         13
#define GSM_LED_PIN         23
#define SYSTEM_LED_PIN      -1
#define MPU6050_ADDRESS     0x69
#define ADXL375_ADDRESS     0x53

// ── NETWORK / DEVICE CONFIG ────────────────────────────────────
const char* APN              = "internet.mtn.rw";
const char* API_URL          = "https://lguard-backend-service.onrender.com/api/v1/telemetry/ingest";
const char* ACCIDENT_API_URL = "https://lguard-backend-service.onrender.com/api/v1/accidents/new";
String DEVICE_ID             = "VEHICLE_001";
String EMERGENCY_CONTACT     = "+250792957781";

// ── SPEED CONFIG ───────────────────────────────────────────────
const float SPEED_LIMIT              = 40.0;
const int   MIN_SATELLITES_FOR_SPEED = 4;
const float MIN_SPEED_THRESHOLD      = 5.0;
const float MIN_GPS_HDOP             = 3.0;

// ── ADXL375 IMPACT THRESHOLDS (±200g RANGE) ───────────────────
const float ADXL375_LOW_THRESHOLD     =   8.0;   //  >8g   LOW
const float ADXL375_MEDIUM_THRESHOLD  =  25.0;   // >25g   MEDIUM
const float ADXL375_HIGH_THRESHOLD    =  50.0;   // >50g   HIGH
const float ADXL375_EXTREME_THRESHOLD = 100.0;   // >100g  EXTREME

// ── MPU6050 / GYRO THRESHOLDS ──────────────────────────────────
const float MPU6050_TILT_THRESHOLD     = 45.0;   // warn tilt
const float MPU6050_ROLLOVER_THRESHOLD = 70.0;   // rollover zone
const float GYRO_ROTATION_THRESHOLD   =  3.0;   // rad/s — moving
const float GYRO_FAST_THRESHOLD       =  6.0;   // rad/s — spinning
const int   ROLLOVER_STABILITY_COUNT  =  3;     // consecutive frames
// v6.1 FIX: rollover must also have some impact to prevent false alarm while parked on slope
const float ROLLOVER_MIN_IMPACT       =  5.0;   // at least 5g during rollover

// ── OTHER THRESHOLDS ──────────────────────────────────────────
const float SPEED_DROP_THRESHOLD = 30.0;

// ── TIMING ────────────────────────────────────────────────────
const unsigned long SENSOR_READ_INTERVAL    =    50UL;
const unsigned long GPS_UPDATE_INTERVAL     =  1000UL;
const unsigned long API_FORCE_SYNC_INTERVAL = 900000UL;  // 15 min force sync
const unsigned long STATUS_PRINT_INTERVAL   =  2000UL;
const unsigned long CANCEL_WINDOW           = 10000UL;
const unsigned long SPEED_BEEP_INTERVAL     =  1000UL;
const unsigned long GPS_WARMUP_TIME         = 10000UL;
const unsigned long SLEEP_IDLE_TIME         = 120000UL;
const unsigned long ACCIDENT_COOLDOWN       = 30000UL;
const unsigned long PEAK_DECAY_INTERVAL     = 60000UL;   // gentle 50% decay every 60s

// ── CHANGE DETECTION THRESHOLDS ───────────────────────────────
const float SPEED_CHANGE_THRESHOLD    = 10.0;
const float LOCATION_CHANGE_THRESHOLD =  0.0002;
const float ACCEL_CHANGE_THRESHOLD    =  5.0;
const float ANGLE_CHANGE_THRESHOLD    = 30.0;

// ── BATTERY ───────────────────────────────────────────────────
const float BATTERY_DIVIDER     = 2.0;
const float ADC_MAX_VALUE       = 4095.0;
const float ADC_REF_VOLTAGE     = 3.3;
const float BATTERY_MIN_VOLTAGE = 3.3;
const float BATTERY_MAX_VOLTAGE = 4.2;

// ══════════════════════════════════════════════════════════════
//  BAND ENUMS — represent discrete sensor threshold zones
//  Any transition between zones triggers an IMMEDIATE API send
// ══════════════════════════════════════════════════════════════
enum ImpactBand { IB_NONE=0, IB_LOW=1, IB_MEDIUM=2, IB_HIGH=3, IB_EXTREME=4 };
enum TiltBand   { TB_FLAT=0, TB_WARN=1, TB_ROLLOVER=2 };
enum SpeedBand  { SB_STOPPED=0, SB_CRAWL=1, SB_NORMAL=2, SB_FAST=3, SB_OVERSPEED=4 };
enum GyroBand   { GB_STILL=0, GB_MOVING=1, GB_SPINNING=2 };

ImpactBand currentImpactBand = IB_NONE;
TiltBand   currentTiltBand   = TB_FLAT;
SpeedBand  currentSpeedBand  = SB_STOPPED;
GyroBand   currentGyroBand   = GB_STILL;

// ── PRIORITY SYNC FLAGS (v6.1) ────────────────────────────────
bool   immediateSync = false;   // true  = send API ASAP
String syncReason    = "";      // injected into JSON + printed to serial

// ── MPU6050 CALIBRATION ────────────────────────────────────────
float rollAngleOffset  = 0;
float pitchAngleOffset = 0;
bool  mpuCalibrated    = false;

// ── HARDWARE OBJECTS ──────────────────────────────────────────
Adafruit_ADXL375 accel = Adafruit_ADXL375(12345);
Adafruit_MPU6050 mpu;
HardwareSerial   modemSerial(1);
HardwareSerial   gpsSerial(2);

// ── GPS ───────────────────────────────────────────────────────
String gpsBuffer  = "";
bool   gpsFixed   = false;
float  currentLat = 0, currentLon = 0, altitude = 0;
float  currentSpeed = 0, previousSpeed = 0, heading = 0;
int    satellites = 0;
float  hdop       = 99.9;
String utcTime    = "";
String fixQuality = "No Fix";

// ── ADXL375 READINGS ──────────────────────────────────────────
float adxl375X = 0, adxl375Y = 0, adxl375Z = 0, adxl375Total = 0;
float adxl375MaxImpact = 0;   // highest g seen since last reset

// ── MPU6050 READINGS ──────────────────────────────────────────
float mpuAccelX = 0, mpuAccelY = 0, mpuAccelZ = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0, totalGyro = 0;
float pitchAngle = 0, rollAngle = 0;
int   rolloverCount = 0;

// ══════════════════════════════════════════════════════════════
//  PEAK TRACKING  (v6.1 — printed in Serial + sent to API)
// ══════════════════════════════════════════════════════════════
float peakImpactG   = 0;
float peakGyroRad   = 0;
float peakRollDeg   = 0;
float peakPitchDeg  = 0;
float peakSpeedKmh  = 0;
unsigned long lastPeakDecay = 0;

// ── ACCIDENT STATE ────────────────────────────────────────────
bool   accidentDetected    = false;
bool   smsSent             = false;
bool   cancelPressed       = false;
unsigned long accidentTime        = 0;
unsigned long lastAccidentHandled = 0;
String accidentType = "";
String impactLevel  = "";

// ── SPEED ALERT STATE ─────────────────────────────────────────
bool          isSpeeding        = false;
unsigned long lastSpeedBeep     = 0;
bool          speedAlertEnabled = false;

// ── SYSTEM STATE ──────────────────────────────────────────────
unsigned long systemStartTime = 0;
bool modemReady    = false;
bool gprsConnected = false;
bool mpuAvailable  = false;

// ── TIMING TRACKERS ───────────────────────────────────────────
unsigned long lastSensorRead        = 0;
unsigned long lastGpsUpdate         = 0;
unsigned long lastApiSync           = 0;
unsigned long lastApiSent           = 0;
unsigned long lastStatusPrint       = 0;
unsigned long lastSignificantChange = 0;
unsigned long lastMovementTime      = 0;

// ── API SYNC CONTROL ──────────────────────────────────────────
bool apiSyncInProgress = false;
bool forceNextSync     = false;

// ── CHANGE DETECTION: PREVIOUS SENT VALUES ────────────────────
float prevSentLat           = 0;
float prevSentLon           = 0;
float prevSentSpeed         = 0;
float prevSentAccelTotal    = 0;
float prevSentRollAngle     = 0;
int   prevSentBatteryPercent = 0;

// ── LED BLINK STATE ───────────────────────────────────────────
unsigned long lastDangerBlink = 0, lastGpsBlink = 0, lastGsmBlink = 0;
bool dangerLedState = false, gpsLedState = false, gsmLedState = false;

// ══════════════════════════════════════════════════════════════
//  FUNCTION PROTOTYPES
// ══════════════════════════════════════════════════════════════
void       setupPins();
void       setupSensors();
void       displayDataRate();
void       setupGPS();
void       setupModem();
bool       connectGPRS();
void       readADXL375();
void       readMPU6050();
void       updatePeaks();
void       decayPeaks();
void       checkRangeChange();
ImpactBand getImpactBand(float g);
TiltBand   getTiltBand();
SpeedBand  getSpeedBand(float spd);
GyroBand   getGyroBand(float gyro);
String     impactBandName(ImpactBand b);
String     tiltBandName(TiltBand b);
String     speedBandName(SpeedBand b);
String     gyroBandName(GyroBand b);
void       handleGPS();
void       processGPSSentence(String sentence);
void       parseGPGGA(String sentence);
void       parseGPRMC(String sentence);
int        splitString(String data, char sep, String* result, int maxParts);
float      convertDMtoDecimal(String coord, String direction);
void       checkSpeedLimit();
void       checkAccident();
void       handleCancelButton();
void       sendEmergencySMS();
bool       sendSingleSMS(String message);
void       sendAccidentToAPI();
void       sendTrackingDataToAPI();
bool       hasSignificantChange();
void       updateStatusLEDs();
String     sendATCommand(String cmd, unsigned long timeout = 1000);
String     readModemResponse(unsigned long timeout = 2000);
bool       httpPOST_Fixed(String jsonData);
bool       httpPOST_Accident(String jsonData);
float      readBatteryVoltage();
int        batteryPercentFromVoltage(float v);
String     buildTelemetryJSON();
String     buildAccidentJSON();
void       printCompactStatus();
bool       isSystemReallyStable();

// ══════════════════════════════════════════════════════════════
//  BAND HELPER FUNCTIONS
// ══════════════════════════════════════════════════════════════

ImpactBand getImpactBand(float g) {
  if (g > ADXL375_EXTREME_THRESHOLD) return IB_EXTREME;
  if (g > ADXL375_HIGH_THRESHOLD)    return IB_HIGH;
  if (g > ADXL375_MEDIUM_THRESHOLD)  return IB_MEDIUM;
  if (g > ADXL375_LOW_THRESHOLD)     return IB_LOW;
  return IB_NONE;
}

TiltBand getTiltBand() {
  if (!mpuAvailable) return TB_FLAT;
  float maxAngle = max(abs(rollAngle), abs(pitchAngle));
  if (maxAngle > MPU6050_ROLLOVER_THRESHOLD) return TB_ROLLOVER;
  if (maxAngle > MPU6050_TILT_THRESHOLD)     return TB_WARN;
  return TB_FLAT;
}

SpeedBand getSpeedBand(float spd) {
  if (spd >= SPEED_LIMIT)         return SB_OVERSPEED;
  if (spd >= 20.0)                return SB_FAST;
  if (spd >= MIN_SPEED_THRESHOLD) return SB_NORMAL;
  if (spd >  0.5)                 return SB_CRAWL;
  return SB_STOPPED;
}

GyroBand getGyroBand(float gyro) {
  if (gyro > GYRO_FAST_THRESHOLD)     return GB_SPINNING;
  if (gyro > GYRO_ROTATION_THRESHOLD) return GB_MOVING;
  return GB_STILL;
}

String impactBandName(ImpactBand b) {
  switch (b) {
    case IB_EXTREME: return "EXTREME>100g";
    case IB_HIGH:    return "HIGH>50g";
    case IB_MEDIUM:  return "MEDIUM>25g";
    case IB_LOW:     return "LOW>8g";
    default:         return "NONE";
  }
}
String tiltBandName(TiltBand b) {
  switch (b) {
    case TB_ROLLOVER: return "ROLLOVER>70deg";
    case TB_WARN:     return "WARN>45deg";
    default:          return "FLAT";
  }
}
String speedBandName(SpeedBand b) {
  switch (b) {
    case SB_OVERSPEED: return "OVER>40kmh";
    case SB_FAST:      return "FAST20-40";
    case SB_NORMAL:    return "NORMAL5-20";
    case SB_CRAWL:     return "CRAWL<5";
    default:           return "STOPPED";
  }
}
String gyroBandName(GyroBand b) {
  switch (b) {
    case GB_SPINNING: return "SPINNING>6";
    case GB_MOVING:   return "MOVING>3";
    default:          return "STILL";
  }
}

// ══════════════════════════════════════════════════════════════
//  RANGE CHANGE DETECTOR
//  Runs every sensor tick. Any band crossing → immediateSync.
// ══════════════════════════════════════════════════════════════
void checkRangeChange() {
  ImpactBand newImpact = getImpactBand(adxl375Total);
  TiltBand   newTilt   = getTiltBand();
  SpeedBand  newSpeed  = getSpeedBand(currentSpeed);
  GyroBand   newGyro   = getGyroBand(totalGyro);

  bool changed = false;
  String reasons = "";

  if (newImpact != currentImpactBand) {
    Serial.printf("\n⚡ IMPACT BAND: [%s] → [%s]  (%.2fg)\n",
                  impactBandName(currentImpactBand).c_str(),
                  impactBandName(newImpact).c_str(),
                  adxl375Total);
    currentImpactBand = newImpact;
    reasons += "IMPACT:" + impactBandName(newImpact) + " ";
    changed = true;
  }

  if (newTilt != currentTiltBand) {
    Serial.printf("\n↗️  TILT BAND:   [%s] → [%s]  (R:%.1f° P:%.1f°)\n",
                  tiltBandName(currentTiltBand).c_str(),
                  tiltBandName(newTilt).c_str(),
                  rollAngle, pitchAngle);
    currentTiltBand = newTilt;
    reasons += "TILT:" + tiltBandName(newTilt) + " ";
    changed = true;
  }

  if (newSpeed != currentSpeedBand) {
    Serial.printf("\n🚀 SPEED BAND:  [%s] → [%s]  (%.1f km/h)\n",
                  speedBandName(currentSpeedBand).c_str(),
                  speedBandName(newSpeed).c_str(),
                  currentSpeed);
    currentSpeedBand = newSpeed;
    reasons += "SPEED:" + speedBandName(newSpeed) + " ";
    changed = true;
  }

  if (newGyro != currentGyroBand) {
    Serial.printf("\n🔄 GYRO BAND:   [%s] → [%s]  (%.2f rad/s)\n",
                  gyroBandName(currentGyroBand).c_str(),
                  gyroBandName(newGyro).c_str(),
                  totalGyro);
    currentGyroBand = newGyro;
    reasons += "GYRO:" + gyroBandName(newGyro) + " ";
    changed = true;
  }

  if (changed) {
    syncReason    = reasons;
    syncReason.trim();
    immediateSync = true;
    forceNextSync = true;
    lastMovementTime = millis();
    Serial.println("🔴 PRIORITY SYNC QUEUED: " + syncReason + "\n");
  }
}

// ══════════════════════════════════════════════════════════════
//  PEAK TRACKING
// ══════════════════════════════════════════════════════════════
void updatePeaks() {
  if (adxl375Total   > peakImpactG)  peakImpactG  = adxl375Total;
  if (totalGyro      > peakGyroRad)  peakGyroRad  = totalGyro;
  if (abs(rollAngle) > peakRollDeg)  peakRollDeg  = abs(rollAngle);
  if (abs(pitchAngle)> peakPitchDeg) peakPitchDeg = abs(pitchAngle);
  if (currentSpeed   > peakSpeedKmh) peakSpeedKmh = currentSpeed;

  // Keep adxl375MaxImpact aligned (used by SMS + accident JSON)
  if (adxl375Total > adxl375MaxImpact) adxl375MaxImpact = adxl375Total;
}

void decayPeaks() {
  unsigned long now = millis();
  if (now - lastPeakDecay < PEAK_DECAY_INTERVAL) return;
  lastPeakDecay = now;

  // 50% decay — remembers the event but slowly fades between 60s windows
  peakImpactG      = max(1.0f, peakImpactG  * 0.5f);
  peakGyroRad      = max(0.0f, peakGyroRad  * 0.5f);
  peakRollDeg      = max(0.0f, peakRollDeg  * 0.5f);
  peakPitchDeg     = max(0.0f, peakPitchDeg * 0.5f);
  peakSpeedKmh     = max(0.0f, peakSpeedKmh * 0.5f);
  adxl375MaxImpact = max(1.0f, adxl375MaxImpact * 0.5f);

  Serial.println("〔 Peak values decayed (60s window) 〕");
}

// ══════════════════════════════════════════════════════════════
//  SETUP FUNCTIONS
// ══════════════════════════════════════════════════════════════
void setupPins() {
  auto pinOut = [](int pin, bool initHigh = false) {
    if (pin >= 0) { pinMode(pin, OUTPUT); digitalWrite(pin, initHigh ? HIGH : LOW); }
  };
  pinOut(SAFE_LED_PIN);
  pinOut(DANGER_LED_PIN);
  pinOut(GPS_LED_PIN);
  pinOut(GSM_LED_PIN);
  pinOut(SYSTEM_LED_PIN);
  pinOut(BUZZER_PIN);
  pinOut(MODEM_PWR_PIN,   true);   // HIGH = modem default
  pinOut(MODEM_DTR_PIN,   true);
  pinOut(MODEM_RESET_PIN, false);

  pinMode(OK_BUTTON_PIN,    INPUT_PULLUP);
  pinMode(MENU_BUTTON_PIN,  INPUT_PULLUP);
  pinMode(BIKE_MONITOR_PIN, INPUT);
  pinMode(MODEM_RI_PIN,     INPUT);
}

void displayDataRate() {
  Serial.print("    Data Rate: ");
  switch (accel.getDataRate()) {
    case ADXL343_DATARATE_3200_HZ: Serial.print("3200"); break;
    case ADXL343_DATARATE_1600_HZ: Serial.print("1600"); break;
    case ADXL343_DATARATE_800_HZ:  Serial.print("800");  break;
    case ADXL343_DATARATE_400_HZ:  Serial.print("400");  break;
    case ADXL343_DATARATE_200_HZ:  Serial.print("200");  break;
    case ADXL343_DATARATE_100_HZ:  Serial.print("100");  break;
    case ADXL343_DATARATE_50_HZ:   Serial.print("50");   break;
    case ADXL343_DATARATE_25_HZ:   Serial.print("25");   break;
    case ADXL343_DATARATE_12_5_HZ: Serial.print("12.5"); break;
    case ADXL343_DATARATE_6_25HZ:  Serial.print("6.25"); break;
    default:                       Serial.print("?");    break;
  }
  Serial.println(" Hz");
}

void setupSensors() {
  Serial.println("Initializing sensors...");
  Wire.begin(21, 22);

  // ── ADXL375 ──────────────────────────────────────────────────
  Serial.print("  . ADXL375 (±200g)... ");
  if (!accel.begin(ADXL375_ADDRESS)) {
    Serial.println("FAILED — check wiring!");
    while (1) {
      if (DANGER_LED_PIN >= 0) {
        digitalWrite(DANGER_LED_PIN, HIGH); delay(200);
        digitalWrite(DANGER_LED_PIN, LOW);  delay(200);
      }
    }
  }
  accel.setDataRate(ADXL343_DATARATE_100_HZ);
  Serial.println("OK");
  accel.printSensorDetails();
  displayDataRate();
  Serial.println("    Range: ±200g (FIXED)\n");

  // ── MPU6050 ──────────────────────────────────────────────────
  Serial.print("  . MPU6050... ");
  if (!mpu.begin(MPU6050_ADDRESS)) {
    Serial.println("NOT DETECTED — tilt/rollover disabled");
    mpuAvailable = false;
  } else {
    Serial.println("OK");
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    mpuAvailable = true;

    // Calibrate — device must be still
    Serial.print("  . Calibrating MPU6050 (keep still)... ");
    delay(800);
    sensors_event_t a, g, temp;
    float sumR = 0, sumP = 0;
    for (int i = 0; i < 20; i++) {
      mpu.getEvent(&a, &g, &temp);
      sumP += atan2(a.acceleration.y,
                    sqrt(a.acceleration.x * a.acceleration.x +
                         a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
      sumR += atan2(-a.acceleration.x, a.acceleration.z) * 180.0 / PI;
      delay(30);
    }
    rollAngleOffset  = sumR / 20.0;
    pitchAngleOffset = sumP / 20.0;
    mpuCalibrated    = true;
    Serial.println("OK");
    Serial.printf("    Roll offset:  %.2f°\n", rollAngleOffset);
    Serial.printf("    Pitch offset: %.2f°\n", pitchAngleOffset);
    Serial.println("    ✅ Current position set as ZERO");
  }

  Serial.println("\n✅ Sensors ready!\n");
}

void setupGPS() {
  Serial.println("Initializing GPS...");
  gpsSerial.begin(9600, SERIAL_8N1, GPS_MODULE_TX, GPS_MODULE_RX);
  Serial.println("✅ GPS started\n");
  delay(1000);
}

void setupModem() {
  Serial.println("Initializing SIM A7670E...");
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
  for (int i = 0; i < 10; i++) {
    String nr = sendATCommand("AT+CREG?", 2000);
    if (nr.indexOf("+CREG: 0,1") >= 0 || nr.indexOf("+CREG: 0,5") >= 0) {
      Serial.println("OK"); modemReady = true; break;
    }
    delay(2000);
  }
  if (!modemReady) { Serial.println("FAILED"); return; }

  sendATCommand("AT+CMGF=1",          1000);
  sendATCommand("AT+CSCS=\"GSM\"",    1000);
  sendATCommand("AT+CSMP=17,167,0,0", 1000);
  sendATCommand("AT+CNMI=2,1,0,0,0",  1000);

  if (sendATCommand("AT+CSCA?", 2000).indexOf("+CSCA:") < 0)
    sendATCommand("AT+CSCA=\"+250788110000\"", 2000);

  Serial.println("✅ Modem initialized!\n");
}

bool connectGPRS() {
  Serial.println("Connecting to GPRS...");
  if (!modemReady) { Serial.println("  ✗ Modem not ready"); return false; }

  sendATCommand("AT+CGATT=1", 2000);
  sendATCommand("AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 2000);

  Serial.print("  . Activating... ");
  if (sendATCommand("AT+CGACT=1,1", 10000).indexOf("OK") >= 0) {
    if (sendATCommand("AT+CGPADDR=1", 2000).indexOf("+CGPADDR") >= 0) {
      Serial.println("OK"); gprsConnected = true; return true;
    }
  }
  Serial.println("FAILED"); gprsConnected = false; return false;
}

// ══════════════════════════════════════════════════════════════
//  setup()
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n╔══════════════════════════════════════════════════════╗");
  Serial.println("║   L-GUARD v6.1 — PRIORITY SYNC + PEAK TRACKING      ║");
  Serial.println("╚══════════════════════════════════════════════════════╝\n");

  systemStartTime  = millis();
  lastMovementTime = millis();
  lastPeakDecay    = millis();

  setupPins();

  // LED startup flash
  for (int i = 0; i < 3; i++) {
    if (SAFE_LED_PIN   >= 0) digitalWrite(SAFE_LED_PIN,   HIGH);
    if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, HIGH);
    if (GPS_LED_PIN    >= 0) digitalWrite(GPS_LED_PIN,    HIGH);
    if (GSM_LED_PIN    >= 0) digitalWrite(GSM_LED_PIN,    HIGH);
    delay(200);
    if (SAFE_LED_PIN   >= 0) digitalWrite(SAFE_LED_PIN,   LOW);
    if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW);
    if (GPS_LED_PIN    >= 0) digitalWrite(GPS_LED_PIN,    LOW);
    if (GSM_LED_PIN    >= 0) digitalWrite(GSM_LED_PIN,    LOW);
    delay(200);
  }

  setupSensors();
  setupGPS();
  setupModem();

  if (modemReady) {
    if (connectGPRS()) {
      Serial.println("✅ GPRS connected!");
      if (SYSTEM_LED_PIN >= 0) digitalWrite(SYSTEM_LED_PIN, HIGH);
    } else {
      Serial.println("⚠️  GPRS failed — SMS only");
    }
  }

  if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);

  // Seed prev-sent values
  prevSentLat            = currentLat;
  prevSentLon            = currentLon;
  prevSentSpeed          = currentSpeed;
  prevSentAccelTotal     = adxl375Total;
  prevSentRollAngle      = rollAngle;
  prevSentBatteryPercent = batteryPercentFromVoltage(readBatteryVoltage());
  lastApiSent = millis();

  Serial.println("\n╔══════════════════════════════════════════════════════╗");
  Serial.println("║  ACCIDENT DETECTION  : ACTIVE  (±200g range)        ║");
  Serial.println("║  PRIORITY SYNC       : ACTIVE  (band-change trigger) ║");
  Serial.println("║  PEAK TRACKING       : ACTIVE  (60s decay)           ║");
  Serial.println("║  ROLLOVER FIX        : ACTIVE  (tilt + impact)       ║");
  Serial.println("║  GPS TRACKING        : ACTIVE                        ║");
  Serial.println("║  SPEED ALERT         : > 40 km/h                     ║");
  Serial.println("║  SMS ALERTS          : ENABLED                       ║");
  Serial.println("╚══════════════════════════════════════════════════════╝");
  Serial.println("\n⏳ Waiting 10s for GPS to stabilize...\n");
}

// ══════════════════════════════════════════════════════════════
//  SENSOR READS
// ══════════════════════════════════════════════════════════════
void readADXL375() {
  sensors_event_t event;
  accel.getEvent(&event);

  const float MPS2_TO_G = 1.0 / 9.80665;
  adxl375X     = event.acceleration.x * MPS2_TO_G;
  adxl375Y     = event.acceleration.y * MPS2_TO_G;
  adxl375Z     = event.acceleration.z * MPS2_TO_G;
  adxl375Total = sqrt(adxl375X*adxl375X + adxl375Y*adxl375Y + adxl375Z*adxl375Z);

  // Movement tracking for sleep mode
  static float lastAccelMove = 1.0;
  if ((abs(currentSpeed - previousSpeed) > 2.0) ||
      (abs(adxl375Total - lastAccelMove) > 0.5)  ||
      currentSpeed > 5.0) {
    lastMovementTime = millis();
  }
  lastAccelMove = adxl375Total;
}

void readMPU6050() {
  if (!mpuAvailable) { rolloverCount = 0; return; }

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  mpuAccelX = a.acceleration.x;
  mpuAccelY = a.acceleration.y;
  mpuAccelZ = a.acceleration.z;
  gyroX = g.gyro.x; gyroY = g.gyro.y; gyroZ = g.gyro.z;
  totalGyro = sqrt(gyroX*gyroX + gyroY*gyroY + gyroZ*gyroZ);

  float rawPitch = atan2(mpuAccelY,
                         sqrt(mpuAccelX*mpuAccelX + mpuAccelZ*mpuAccelZ)) * 180.0 / PI;
  float rawRoll  = atan2(-mpuAccelX, mpuAccelZ) * 180.0 / PI;

  pitchAngle = rawPitch - (mpuCalibrated ? pitchAngleOffset : 0.0f);
  rollAngle  = rawRoll  - (mpuCalibrated ? rollAngleOffset  : 0.0f);

  // Rollover counter
  if (abs(rollAngle) > MPU6050_ROLLOVER_THRESHOLD ||
      abs(pitchAngle) > MPU6050_ROLLOVER_THRESHOLD) {
    rolloverCount = constrain(rolloverCount + 1, 0, ROLLOVER_STABILITY_COUNT);
  } else {
    rolloverCount = 0;
  }
}

// ══════════════════════════════════════════════════════════════
//  GPS PARSING (unchanged from v6.0)
// ══════════════════════════════════════════════════════════════
void handleGPS() {
  while (gpsSerial.available()) gpsBuffer += char(gpsSerial.read());

  int nlPos;
  while ((nlPos = gpsBuffer.indexOf('\n')) != -1) {
    String sentence = gpsBuffer.substring(0, nlPos);
    sentence.trim();
    gpsBuffer = gpsBuffer.substring(nlPos + 1);
    if (sentence.length() > 6) processGPSSentence(sentence);
  }
  if (gpsBuffer.length() > 1000) gpsBuffer = "";
}

void processGPSSentence(String sentence) {
  if (sentence.startsWith("$GPGGA") || sentence.startsWith("$GNGGA")) parseGPGGA(sentence);
  else if (sentence.startsWith("$GPRMC") || sentence.startsWith("$GNRMC")) parseGPRMC(sentence);
}

void parseGPGGA(String sentence) {
  String parts[15];
  if (splitString(sentence, ',', parts, 15) < 10) return;

  if (parts[1].length() >= 6)
    utcTime = parts[1].substring(0,2) + ":" + parts[1].substring(2,4) + ":" + parts[1].substring(4,6);

  int fixNum = parts[6].toInt();
  switch (fixNum) {
    case 0:  fixQuality = "No Fix";   gpsFixed = false; break;
    case 1:  fixQuality = "GPS Fix";  gpsFixed = true;  break;
    case 2:  fixQuality = "DGPS Fix"; gpsFixed = true;  break;
    default: fixQuality = "Unknown";  gpsFixed = false;
  }

  satellites = parts[7].toInt();
  if (parts[8].length() > 0) hdop = parts[8].toFloat();

  if (gpsFixed && parts[2].length() > 0 && parts[4].length() > 0) {
    currentLat = convertDMtoDecimal(parts[2], parts[3]);
    currentLon = convertDMtoDecimal(parts[4], parts[5]);
    if (parts[9].length() > 0) altitude = parts[9].toFloat();
  }
}

void parseGPRMC(String sentence) {
  String parts[15];
  if (splitString(sentence, ',', parts, 15) < 8) return;
  if (parts[2] != "A") return;

  if (parts[7].length() > 0) {
    previousSpeed = currentSpeed;
    currentSpeed  = parts[7].toFloat() * 1.852;
    if (currentSpeed < 0 || currentSpeed > 300) currentSpeed = previousSpeed;
    if (!gpsFixed || satellites < MIN_SATELLITES_FOR_SPEED || hdop > MIN_GPS_HDOP) {
      if (abs(currentSpeed - previousSpeed) > 20) currentSpeed = previousSpeed;
    }
  }
  if (parts[8].length() > 0) heading = parts[8].toFloat();
}

int splitString(String data, char sep, String* result, int maxParts) {
  int idx = 0, last = 0;
  for (int i = 0; i <= (int)data.length() && idx < maxParts; i++) {
    if (i == (int)data.length() || data.charAt(i) == sep) {
      result[idx++] = data.substring(last, i);
      last = i + 1;
    }
  }
  return idx;
}

float convertDMtoDecimal(String coord, String direction) {
  if (coord.length() < 4) return 0.0;
  float deg, min;
  int dot = coord.indexOf('.');
  if (dot > 4) { deg = coord.substring(0,3).toFloat(); min = coord.substring(3).toFloat(); }
  else         { deg = coord.substring(0,2).toFloat(); min = coord.substring(2).toFloat(); }
  float dec = deg + (min / 60.0);
  if (direction == "S" || direction == "W") dec = -dec;
  return dec;
}

// ══════════════════════════════════════════════════════════════
//  SPEED ALERT
// ══════════════════════════════════════════════════════════════
void checkSpeedLimit() {
  if (!speedAlertEnabled) { digitalWrite(BUZZER_PIN, LOW); isSpeeding = false; return; }

  bool reliable = (gpsFixed && satellites >= MIN_SATELLITES_FOR_SPEED &&
                   hdop < MIN_GPS_HDOP && currentSpeed > MIN_SPEED_THRESHOLD);

  if (reliable && currentSpeed > SPEED_LIMIT) {
    if (!isSpeeding) {
      isSpeeding = true;
      Serial.println("\n⚠️  OVERSPEEDING: " + String(currentSpeed, 1) + " km/h");
      lastMovementTime = millis();
    }
    unsigned long now = millis();
    if (now - lastSpeedBeep >= SPEED_BEEP_INTERVAL) {
      digitalWrite(BUZZER_PIN, HIGH); delay(200); digitalWrite(BUZZER_PIN, LOW);
      lastSpeedBeep = now;
    }
  } else {
    if (isSpeeding) { Serial.println("✅ Speed normal"); isSpeeding = false; }
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ══════════════════════════════════════════════════════════════
//  ACCIDENT DETECTION  (v6.1: rollover fix + full priority ladder)
//
//  Priority order (highest first):
//    1. CATASTROPHIC_IMPACT  — >100g
//    2. SEVERE_IMPACT        — >50g
//    3. ROLLOVER             — sustained tilt >70° AND >5g  ← FIX
//    4. SPEED_DROP_COLLISION — sudden speed drop + >25g
//    5. IMPACT_COLLISION     — >25g
//    6. VEHICLE_FLIP         — tilt >45° + gyro >3 rad/s
// ══════════════════════════════════════════════════════════════
void checkAccident() {
  if (millis() - lastAccidentHandled < ACCIDENT_COOLDOWN) return;
  if (accidentDetected) return;

  static bool emergencyInterruptNeeded = false;
  if (emergencyInterruptNeeded && apiSyncInProgress) {
    Serial.println("\n⚠️  EMERGENCY: Interrupting API sync for accident!");
    sendATCommand("AT+HTTPTERM", 500);
    apiSyncInProgress        = false;
    emergencyInterruptNeeded = false;
  }

  // ── ADXL375 levels ─────────────────────────────────────────
  bool extremeImpact = (adxl375Total > ADXL375_EXTREME_THRESHOLD);
  bool highImpact    = (adxl375Total > ADXL375_HIGH_THRESHOLD);
  bool mediumImpact  = (adxl375Total > ADXL375_MEDIUM_THRESHOLD);

  // ── MPU6050 conditions ─────────────────────────────────────
  // FIX: rollover requires sustained tilt + real impact — no more false alarms on slopes
  bool rolloverSustained = mpuAvailable
    ? (rolloverCount >= ROLLOVER_STABILITY_COUNT && adxl375Total > ROLLOVER_MIN_IMPACT)
    : false;
  bool tiltWarning   = mpuAvailable
    ? (abs(rollAngle) > MPU6050_TILT_THRESHOLD || abs(pitchAngle) > MPU6050_TILT_THRESHOLD)
    : false;
  bool rapidRotation = mpuAvailable ? (totalGyro > GYRO_ROTATION_THRESHOLD) : false;

  // ── Speed drop ─────────────────────────────────────────────
  bool speedDrop = (gpsFixed && previousSpeed > 40.0 && currentSpeed < 10.0
                    && (previousSpeed - currentSpeed) >= SPEED_DROP_THRESHOLD);

  bool accidentCondition = false;

  if (extremeImpact) {
    accidentType = "CATASTROPHIC_IMPACT"; impactLevel = "EXTREME"; accidentCondition = true;
  } else if (highImpact) {
    accidentType = "SEVERE_IMPACT";       impactLevel = "HIGH";    accidentCondition = true;
  } else if (rolloverSustained) {
    accidentType = "ROLLOVER";            impactLevel = "HIGH";    accidentCondition = true;
  } else if (speedDrop && mediumImpact) {
    accidentType = "SPEED_DROP_COLLISION"; impactLevel = "HIGH";   accidentCondition = true;
  } else if (mediumImpact) {
    accidentType = "IMPACT_COLLISION";    impactLevel = "MEDIUM";  accidentCondition = true;
  } else if (tiltWarning && rapidRotation) {
    accidentType = "VEHICLE_FLIP";        impactLevel = "MEDIUM";  accidentCondition = true;
  }

  if (accidentCondition) {
    accidentDetected = true;
    accidentTime     = millis();
    cancelPressed    = false;
    smsSent          = false;

    if (apiSyncInProgress) emergencyInterruptNeeded = true;
    if (SAFE_LED_PIN >= 0)  digitalWrite(SAFE_LED_PIN, LOW);

    // ── Detailed accident Serial print (live + peak values) ──
    Serial.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("          🚨 ACCIDENT DETECTED! 🚨");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("  Type         : " + accidentType);
    Serial.println("  Severity     : " + impactLevel);
    Serial.printf( "  Live Impact  : %.2fg\n",   adxl375Total);
    Serial.printf( "  Peak Impact  : %.2fg  ← peak since last decay\n", peakImpactG);
    Serial.printf( "  Live Gyro    : %.2f rad/s\n", totalGyro);
    Serial.printf( "  Peak Gyro    : %.2f rad/s\n", peakGyroRad);
    Serial.printf( "  Roll         : %.1f°  (peak %.1f°)\n", rollAngle, peakRollDeg);
    Serial.printf( "  Pitch        : %.1f°  (peak %.1f°)\n", pitchAngle, peakPitchDeg);
    Serial.printf( "  Speed        : %.1f km/h  (peak %.1f km/h)\n", currentSpeed, peakSpeedKmh);
    Serial.printf( "  RolloverCnt  : %d / %d\n", rolloverCount, ROLLOVER_STABILITY_COUNT);
    if (gpsFixed)
      Serial.printf("  Location     : %.6f, %.6f\n", currentLat, currentLon);
    else
      Serial.println("  Location     : GPS NO FIX");
    Serial.println("\n  ⏰ Press CANCEL (OK button) within 10s!");
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    lastMovementTime = millis();
  }
}

// ══════════════════════════════════════════════════════════════
//  CANCEL BUTTON
// ══════════════════════════════════════════════════════════════
void handleCancelButton() {
  unsigned long elapsed = millis() - accidentTime;

  if (digitalRead(OK_BUTTON_PIN) == LOW) {
    delay(50);
    if (digitalRead(OK_BUTTON_PIN) == LOW) {
      cancelPressed       = true;
      accidentDetected    = false;
      smsSent             = false;
      adxl375MaxImpact    = 0;
      rolloverCount       = 0;
      lastAccidentHandled = millis();

      digitalWrite(BUZZER_PIN, LOW);
      if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW);
      if (SAFE_LED_PIN   >= 0) digitalWrite(SAFE_LED_PIN,   HIGH);

      Serial.println("\n✅ ALERT CANCELLED BY USER\n");
      while (digitalRead(OK_BUTTON_PIN) == LOW) delay(10);
      return;
    }
  }

  if (elapsed >= CANCEL_WINDOW && !cancelPressed && !smsSent) {
    Serial.println("\n⏰ CANCEL WINDOW EXPIRED — sending alerts!\n");

    if (apiSyncInProgress) {
      Serial.println("⚠️  Interrupting API sync for emergency!");
      sendATCommand("AT+HTTPTERM", 1000);
      apiSyncInProgress = false;
    }

    sendEmergencySMS();
    delay(3000);
    sendATCommand("AT",        500);
    sendATCommand("AT+CMGF=1", 500);

    if (gprsConnected) sendAccidentToAPI();

    accidentDetected    = false;
    adxl375MaxImpact    = 0;
    rolloverCount       = 0;
    lastAccidentHandled = millis();
    forceNextSync       = true;

    if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
    Serial.println("\n✅ Accident handled — monitoring resumed\n");
  }
}

// ══════════════════════════════════════════════════════════════
//  SIGNIFICANT CHANGE DETECTION (legacy threshold-based)
//  Runs in addition to band-based detection for slow drifts
// ══════════════════════════════════════════════════════════════
bool hasSignificantChange() {
  float latDiff   = gpsFixed ? abs(currentLat - prevSentLat) : 0;
  float lonDiff   = gpsFixed ? abs(currentLon - prevSentLon) : 0;
  float speedDiff = abs(currentSpeed  - prevSentSpeed);
  float accelDiff = abs(adxl375Total  - prevSentAccelTotal);
  float angleDiff = mpuAvailable ? abs(rollAngle - prevSentRollAngle) : 0;
  int   curBatt   = batteryPercentFromVoltage(readBatteryVoltage());

  bool anyChange =
    (latDiff   > LOCATION_CHANGE_THRESHOLD) ||
    (lonDiff   > LOCATION_CHANGE_THRESHOLD) ||
    (speedDiff > SPEED_CHANGE_THRESHOLD)    ||
    (accelDiff > ACCEL_CHANGE_THRESHOLD)    ||
    (angleDiff > ANGLE_CHANGE_THRESHOLD)    ||
    (abs(curBatt - prevSentBatteryPercent) >= 20) ||
    ((prevSentSpeed > MIN_SPEED_THRESHOLD) != (currentSpeed > MIN_SPEED_THRESHOLD));

  // Ignore battery-only drift while parked
  if (anyChange &&
      latDiff   <= LOCATION_CHANGE_THRESHOLD &&
      lonDiff   <= LOCATION_CHANGE_THRESHOLD &&
      speedDiff <= SPEED_CHANGE_THRESHOLD    &&
      accelDiff <= ACCEL_CHANGE_THRESHOLD    &&
      angleDiff <= ANGLE_CHANGE_THRESHOLD    &&
      prevSentSpeed  <= MIN_SPEED_THRESHOLD  &&
      currentSpeed   <= MIN_SPEED_THRESHOLD) {
    return false;
  }

  return anyChange;
}

// ══════════════════════════════════════════════════════════════
//  JSON BUILDERS
// ══════════════════════════════════════════════════════════════
String buildTelemetryJSON() {
  String json = "{";
  json += "\"deviceId\":\"" + DEVICE_ID + "\",";

  if (gpsFixed && currentLat != 0 && currentLon != 0) {
    json += "\"latitude\":"  + String(currentLat, 6) + ",";
    json += "\"longitude\":" + String(currentLon, 6) + ",";
  } else {
    json += "\"latitude\":0,\"longitude\":0,";
  }

  json += "\"altitude\":"    + String((int)altitude)    + ",";
  json += "\"speed\":"       + String(currentSpeed, 1)  + ",";
  json += "\"heading\":"     + String((int)heading)     + ",";
  json += "\"accelerationX\":" + String(adxl375X, 2)   + ",";
  json += "\"accelerationY\":" + String(adxl375Y, 2)   + ",";
  json += "\"accelerationZ\":" + String(adxl375Z, 2)   + ",";
  json += "\"gyroX\":"       + String(gyroX, 2)         + ",";
  json += "\"gyroY\":"       + String(gyroY, 2)         + ",";
  json += "\"gyroZ\":"       + String(gyroZ, 2)         + ",";
  json += "\"rpm\":0,\"engineTemp\":0,\"fuelLevel\":0,";
  json += "\"batteryLevel\":" + String(batteryPercentFromVoltage(readBatteryVoltage())) + ",";
  json += "\"signalStrength\":85,";

  // ── syncReason: tells backend exactly why this packet was sent ──
  json += "\"syncReason\":\"" + (syncReason.length() > 0 ? syncReason : "PERIODIC") + "\",";

  json += "\"rawData\":{";
  json += "\"totalAccel\":"    + String(adxl375Total, 2)  + ",";
  json += "\"totalGyro\":"     + String(totalGyro, 2)     + ",";
  json += "\"pitchAngle\":"    + String(pitchAngle, 1)    + ",";
  json += "\"rollAngle\":"     + String(rollAngle, 1)     + ",";
  // ── Peak values (v6.1) ──────────────────────────────────────
  json += "\"peakImpactG\":"   + String(peakImpactG, 2)   + ",";
  json += "\"peakGyroRad\":"   + String(peakGyroRad, 2)   + ",";
  json += "\"peakRollDeg\":"   + String(peakRollDeg, 1)   + ",";
  json += "\"peakPitchDeg\":"  + String(peakPitchDeg, 1)  + ",";
  json += "\"peakSpeedKmh\":"  + String(peakSpeedKmh, 1)  + ",";
  // ── Band labels ─────────────────────────────────────────────
  json += "\"impactBand\":\""  + impactBandName(currentImpactBand) + "\",";
  json += "\"tiltBand\":\""    + tiltBandName(currentTiltBand)     + "\",";
  json += "\"speedBand\":\""   + speedBandName(currentSpeedBand)   + "\",";
  json += "\"gyroBand\":\""    + gyroBandName(currentGyroBand)     + "\",";
  json += "\"vibration\":false,";
  json += "\"satellites\":"    + String(satellites)    + ",";
  json += "\"hdop\":"          + String(hdop, 1)       + ",";
  json += "\"fixQuality\":\""  + fixQuality            + "\",";
  json += "\"accidentDetected\":" + String(accidentDetected ? "true" : "false");
  if (accidentDetected || smsSent) {
    json += ",\"accidentType\":\"" + accidentType + "\",";
    json += "\"impactLevel\":\""   + impactLevel  + "\",";
    json += "\"maxImpact\":"       + String(adxl375MaxImpact, 1);
  }
  json += "}}";
  return json;
}

String buildAccidentJSON() {
  // Map internal level to API severity enum
  String severity;
  if      (impactLevel == "EXTREME") severity = "SEVERE";
  else if (impactLevel == "HIGH")    severity = "SEVERE";
  else if (impactLevel == "MEDIUM")  severity = "MODERATE";
  else                               severity = "MINOR";

  String json = "{";
  json += "\"deviceId\":\""     + DEVICE_ID     + "\",";
  json += "\"severity\":\""     + severity      + "\",";
  json += "\"accidentType\":\"" + accidentType  + "\",";
  json += "\"speed\":"          + String(currentSpeed, 1)      + ",";
  json += "\"impactG\":"        + String(adxl375MaxImpact, 2)  + ",";
  json += "\"peakImpactG\":"    + String(peakImpactG, 2)       + ",";
  json += "\"rollAngle\":"      + String(rollAngle, 1)         + ",";
  json += "\"pitchAngle\":"     + String(pitchAngle, 1)        + ",";
  json += "\"peakGyroRad\":"    + String(peakGyroRad, 2)       + ",";
  if (gpsFixed && currentLat != 0 && currentLon != 0) {
    json += "\"latitude\":"  + String(currentLat, 6) + ",";
    json += "\"longitude\":" + String(currentLon, 6);
  } else {
    json += "\"latitude\":0.0,\"longitude\":0.0";
  }
  json += "}";
  return json;
}

// ══════════════════════════════════════════════════════════════
//  HTTP POST (unchanged from v6.0)
// ══════════════════════════════════════════════════════════════
bool httpPOST_Fixed(String jsonData) {
  if (!gprsConnected || apiSyncInProgress) return false;
  apiSyncInProgress = true;

  sendATCommand("AT+HTTPTERM",                   1000);
  delay(500);
  sendATCommand("AT+CSSLCFG=\"enableSNI\",0,1",  2000);
  sendATCommand("AT+HTTPINIT",                   2000);
  modemSerial.println("AT+HTTPPARA=\"URL\",\"" + String(API_URL) + "\"");
  delay(1000); readModemResponse(2000);
  modemSerial.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
  delay(1000); readModemResponse(2000);
  modemSerial.println("AT+HTTPDATA=" + String(jsonData.length()) + ",10000");
  delay(1000);
  String dlResp = readModemResponse(3000);

  bool success = false;
  if (dlResp.indexOf("DOWNLOAD") >= 0) {
    modemSerial.print(jsonData);
    delay(2000);
    if (readModemResponse(3000).indexOf("OK") >= 0) {
      modemSerial.println("AT+HTTPACTION=1");
      for (int i = 0; i < 15; i++) {
        delay(500);
        if (modemSerial.available()) {
          String r = readModemResponse(2000);
          if (r.indexOf("+HTTPACTION") >= 0) {
            success = (r.indexOf(",200,") >= 0 || r.indexOf(",201,") >= 0);
            break;
          }
        }
      }
    }
  }

  sendATCommand("AT+HTTPTERM", 1000);
  apiSyncInProgress = false;
  return success;
}

bool httpPOST_Accident(String jsonData) {
  if (!gprsConnected || apiSyncInProgress) return false;
  apiSyncInProgress = true;

  sendATCommand("AT+HTTPTERM",                   1000);
  delay(500);
  sendATCommand("AT+CSSLCFG=\"enableSNI\",0,1",  2000);
  sendATCommand("AT+HTTPINIT",                   2000);
  modemSerial.println("AT+HTTPPARA=\"URL\",\"" + String(ACCIDENT_API_URL) + "\"");
  delay(1000); readModemResponse(2000);
  modemSerial.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
  delay(1000); readModemResponse(2000);
  modemSerial.println("AT+HTTPDATA=" + String(jsonData.length()) + ",10000");
  delay(1000);
  String dlResp = readModemResponse(3000);

  bool success = false;
  if (dlResp.indexOf("DOWNLOAD") >= 0) {
    modemSerial.print(jsonData);
    delay(2000);
    if (readModemResponse(3000).indexOf("OK") >= 0) {
      modemSerial.println("AT+HTTPACTION=1");
      for (int i = 0; i < 15; i++) {
        delay(500);
        if (modemSerial.available()) {
          String r = readModemResponse(2000);
          if (r.indexOf("+HTTPACTION") >= 0) {
            success = (r.indexOf(",200,") >= 0 || r.indexOf(",201,") >= 0);
            Serial.println(success ? "   ✅ ACCIDENT API SUCCESS!" : "   ❌ ACCIDENT API Error: " + r);
            break;
          }
        }
      }
    }
  }

  sendATCommand("AT+HTTPTERM", 1000);
  apiSyncInProgress = false;
  return success;
}

// ══════════════════════════════════════════════════════════════
//  API SEND WRAPPERS
// ══════════════════════════════════════════════════════════════
void sendTrackingDataToAPI() {
  if (apiSyncInProgress) { Serial.println("⚠️  API busy — skipping"); return; }
  Serial.println("\n📤 Telemetry → [" + (syncReason.length() > 0 ? syncReason : "PERIODIC") + "]");

  if (httpPOST_Fixed(buildTelemetryJSON())) {
    prevSentLat            = currentLat;
    prevSentLon            = currentLon;
    prevSentSpeed          = currentSpeed;
    prevSentAccelTotal     = adxl375Total;
    prevSentRollAngle      = rollAngle;
    prevSentBatteryPercent = batteryPercentFromVoltage(readBatteryVoltage());
    lastApiSent            = millis();
    Serial.println("   ✅ Telemetry OK");
  } else {
    Serial.println("   ❌ Telemetry failed");
  }
}

void sendAccidentToAPI() {
  if (apiSyncInProgress) { forceNextSync = true; return; }
  Serial.println("\n🚨 Sending accident → /accidents/new ...");
  if (httpPOST_Accident(buildAccidentJSON())) {
    Serial.println("✅ Accident logged!\n");
    lastApiSent = millis();
  } else {
    Serial.println("❌ Accident logging failed\n");
  }
}

// ══════════════════════════════════════════════════════════════
//  SMS
// ══════════════════════════════════════════════════════════════
void sendEmergencySMS() {
  if (smsSent || !modemReady) return;
  Serial.println("\n📱 SENDING EMERGENCY SMS...");
  sendATCommand("AT+CMGF=1", 1000);
  delay(500);

  String msg1  = "!! ACCIDENT !!\n" + DEVICE_ID + "\n" + accidentType + "\n";
         msg1 += "Impact: " + String(adxl375MaxImpact, 1) + "g  Peak:" + String(peakImpactG, 1) + "g\n";
         msg1 += "Time: " + utcTime;
         msg1 += gpsFixed ? "\nSpeed: " + String(currentSpeed, 0) + "km/h" : "\nGPS: NO FIX";

  bool ok1 = sendSingleSMS(msg1);

  if (ok1 && gpsFixed && currentLat != 0 && currentLon != 0) {
    delay(2000);
    String msg2  = "LOCATION:\n";
           msg2 += String(currentLat, 6) + "," + String(currentLon, 6) + "\n\n";
           msg2 += "Map:\nhttps://maps.google.com/maps?q=";
           msg2 += String(currentLat, 6) + "," + String(currentLon, 6);
    Serial.println("\n📱 Sending location SMS...");
    sendSingleSMS(msg2);
  }

  if (ok1) { smsSent = true; Serial.println("\n✅ EMERGENCY ALERT SENT!"); }
}

bool sendSingleSMS(String message) {
  while (modemSerial.available()) modemSerial.read();
  modemSerial.print("AT+CMGS=\""); modemSerial.print(EMERGENCY_CONTACT); modemSerial.print("\"");
  modemSerial.write('\r');
  delay(1000);

  String prompt = "";
  unsigned long sw = millis();
  while (millis() - sw < 5000) {
    if (modemSerial.available()) { char c = modemSerial.read(); prompt += c; if (c == '>') break; }
  }
  if (prompt.indexOf('>') < 0) { Serial.println("❌ No > prompt"); return false; }

  for (size_t i = 0; i < message.length(); i++) { modemSerial.write(message[i]); delay(5); }
  delay(500);
  modemSerial.write(26); modemSerial.write('\r');

  String resp = "";
  sw = millis();
  while (millis() - sw < 15000) {
    if (modemSerial.available()) { char c = modemSerial.read(); resp += c; }
    if (resp.indexOf("+CMGS:") >= 0 || resp.indexOf("OK") >= 0) { Serial.println("✅ SMS sent"); return true; }
    if (resp.indexOf("ERROR") >= 0) { Serial.println("❌ SMS ERROR: " + resp); return false; }
  }
  Serial.println("❌ SMS Timeout"); return false;
}

// ══════════════════════════════════════════════════════════════
//  LEDs
// ══════════════════════════════════════════════════════════════
void updateStatusLEDs() {
  unsigned long now = millis();

  if (accidentDetected) {
    if (!smsSent) {
      if (now - lastDangerBlink >= 150) {
        dangerLedState = !dangerLedState;
        if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, dangerLedState);
        lastDangerBlink = now;
      }
    } else { if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, HIGH); }
  } else { if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW); }

  if (GPS_LED_PIN >= 0) {
    unsigned long gi = gpsFixed ? 250 : 1000;
    if (now - lastGpsBlink >= gi) { gpsLedState = !gpsLedState; digitalWrite(GPS_LED_PIN, gpsLedState); lastGpsBlink = now; }
  }
  if (GSM_LED_PIN >= 0) {
    unsigned long mi = modemReady ? 300 : 1500;
    if (now - lastGsmBlink >= mi) { gsmLedState = !gsmLedState; digitalWrite(GSM_LED_PIN, gsmLedState); lastGsmBlink = now; }
  }
  if (!accidentDetected && SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
}

// ══════════════════════════════════════════════════════════════
//  AT MODEM HELPERS
// ══════════════════════════════════════════════════════════════
String sendATCommand(String cmd, unsigned long timeout) {
  modemSerial.println(cmd);
  return readModemResponse(timeout);
}

String readModemResponse(unsigned long timeout) {
  String resp = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (modemSerial.available()) { char c = modemSerial.read(); resp += c; }
    if (resp.indexOf("OK\r\n")         >= 0 ||
        resp.indexOf("ERROR\r\n")      >= 0 ||
        resp.indexOf("DOWNLOAD")       >= 0 ||
        resp.indexOf("+CMGS:")         >= 0 ||
        resp.indexOf("+HTTPACTION:")   >= 0) break;
  }
  resp.trim();
  return resp;
}

// ══════════════════════════════════════════════════════════════
//  BATTERY
// ══════════════════════════════════════════════════════════════
float readBatteryVoltage() {
  float total = 0;
  for (int i = 0; i < 5; i++) {
    total += (analogRead(BATTERY_ADC_PIN) / ADC_MAX_VALUE) * ADC_REF_VOLTAGE * BATTERY_DIVIDER;
    delay(10);
  }
  return total / 5.0;
}

int batteryPercentFromVoltage(float v) {
  if (v <= BATTERY_MIN_VOLTAGE) return 0;
  if (v >= BATTERY_MAX_VOLTAGE) return 100;
  return constrain((int)round(((v - BATTERY_MIN_VOLTAGE) /
         (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0), 0, 100);
}

// ══════════════════════════════════════════════════════════════
//  COMPACT STATUS PRINT  (v6.1: peaks + band labels)
// ══════════════════════════════════════════════════════════════
void printCompactStatus() {
  Serial.println("┌─────────────────────────────────────────────────────────────┐");
  Serial.printf( "│ IMPACT  live:%-6.2fg  PEAK:%-6.2fg  Band: %-14s     │\n",
                 adxl375Total, peakImpactG, impactBandName(currentImpactBand).c_str());
  Serial.printf( "│ GYRO    live:%-5.2f   PEAK:%-5.2f   Band: %-14s     │\n",
                 totalGyro, peakGyroRad, gyroBandName(currentGyroBand).c_str());
  Serial.printf( "│ TILT    R:%-5.1f°(pk%-5.1f°)  P:%-5.1f°(pk%-5.1f°)  Band:%-14s│\n",
                 rollAngle, peakRollDeg, pitchAngle, peakPitchDeg,
                 tiltBandName(currentTiltBand).c_str());
  Serial.printf( "│ SPEED   live:%-5.1f  PEAK:%-5.1f  Band:%-14s  %s  │\n",
                 currentSpeed, peakSpeedKmh, speedBandName(currentSpeedBand).c_str(),
                 isSpeeding ? "⚠️ OVERSPEED" : "            ");
  Serial.printf( "│ GPS:%s(%dsat HDOP:%.1f)  Bat:%d%%  GSM:%s  API:%s  Last:%lus │\n",
                 gpsFixed ? "FIX" : "NO ", satellites, hdop,
                 batteryPercentFromVoltage(readBatteryVoltage()),
                 modemReady    ? "OK" : "NO",
                 gprsConnected ? "OK" : "NO",
                 (millis() - lastApiSent) / 1000UL);
  Serial.println("└─────────────────────────────────────────────────────────────┘");
}

// ══════════════════════════════════════════════════════════════
//  SLEEP STABILITY CHECK
// ══════════════════════════════════════════════════════════════
bool isSystemReallyStable() {
  static float  lastAccelCheck = 0, lastRollCheck = 0;
  static unsigned long lastCheck = 0;
  static int stableCount = 0;

  unsigned long now = millis();
  if (now - lastCheck < 2000) return false;

  bool stable = (abs(adxl375Total - lastAccelCheck) < 0.5)
             && (abs(rollAngle    - lastRollCheck)   < 5.0)
             && (currentSpeed < 1.0)
             && (now - lastMovementTime > SLEEP_IDLE_TIME);

  stableCount   = stable ? stableCount + 1 : 0;
  lastAccelCheck = adxl375Total;
  lastRollCheck  = rollAngle;
  lastCheck      = now;

  return (stableCount >= 5);
}

// ══════════════════════════════════════════════════════════════
//  loop()
// ══════════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // ── 1. SENSORS (50ms) ── read → update peaks → detect band changes → accident check
  if (now - lastSensorRead >= SENSOR_READ_INTERVAL) {
    readADXL375();
    readMPU6050();
    updatePeaks();       // always update peaks from latest readings
    checkRangeChange();  // band crossing? → sets immediateSync + syncReason
    checkAccident();
    lastSensorRead = now;
  }

  // ── 2. GPS ────────────────────────────────────────────────
  handleGPS();
  if (now - lastGpsUpdate >= GPS_UPDATE_INTERVAL) {
    checkSpeedLimit();
    lastGpsUpdate = now;
  }

  // ── 3. GPS WARM-UP ────────────────────────────────────────
  if (!speedAlertEnabled && (now - systemStartTime >= GPS_WARMUP_TIME)) {
    speedAlertEnabled = true;
    Serial.println("✅ GPS warmed up — speed alerts ENABLED\n");
    lastSignificantChange = now;
  }

  // ── 4. API SYNC ───────────────────────────────────────────
  //    Priority: immediateSync (band change) → hasSignificantChange → force 15min
  if (gprsConnected && !apiSyncInProgress) {
    bool forceSync  = (now - lastApiSent >= API_FORCE_SYNC_INTERVAL);
    bool hasChanges = hasSignificantChange();

    if (immediateSync || hasChanges || forceSync || forceNextSync) {
      sendTrackingDataToAPI();
      lastApiSync   = now;
      forceNextSync = false;
      immediateSync = false;
      syncReason    = "";
    }
  }

  // ── 5. ACCIDENT CANCEL WINDOW ─────────────────────────────
  if (accidentDetected && !smsSent) handleCancelButton();

  // ── 6. LEDs ───────────────────────────────────────────────
  updateStatusLEDs();

  // ── 7. STATUS PRINT ───────────────────────────────────────
  if (now - lastStatusPrint >= STATUS_PRINT_INTERVAL) {
    if (!accidentDetected && !apiSyncInProgress) printCompactStatus();
    lastStatusPrint = now;
  }

  // ── 8. PEAK DECAY (60s) ───────────────────────────────────
  decayPeaks();

  // ── 9. LIGHT SLEEP (if everything stable for 2+ min) ──────
  if (isSystemReallyStable() && !accidentDetected && !isSpeeding && !apiSyncInProgress) {
    Serial.println("\n💤 SLEEP (all sensors stable)");
    esp_sleep_enable_timer_wakeup(10000000ULL);  // 10s
    esp_light_sleep_start();
    lastMovementTime = millis();
  }

  // ── 10. BUZZER SAFETY OFF ─────────────────────────────────
  if (!isSpeeding && !accidentDetected) digitalWrite(BUZZER_PIN, LOW);

  delay(10);
}
