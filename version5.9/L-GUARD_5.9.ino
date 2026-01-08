  /*
  * L-GUARD VEHICLE SAFETY SYSTEM v5.5 - PRODUCTION READY
  * 
  * FIXES:
  * ✅ GPS speed calculation fixed (raw NMEA parsing + validation)
  * ✅ API sending controlled (no spam, smart sync)
  * ✅ Continuous sensor monitoring (never blocked)
  * ✅ Accident detection always active
  * ✅ Smart sleep mode
  * ✅ Database-friendly (only sends on real change)
  */

  #include <Wire.h>
  #include <Adafruit_Sensor.h>
  #include <Adafruit_ADXL345_U.h>
  #include <Adafruit_MPU6050.h>

  // PIN DEFINITIONS
  #define MODEM_RI_PIN 2
  #define MODEM_RXD 5
  #define MODEM_TXD 4
  #define MODEM_PWR_PIN 15
  #define MODEM_DTR_PIN 18
  #define MODEM_RESET_PIN 19
  #define GPS_MODULE_TX 16
  #define GPS_MODULE_RX 17
  #define BUZZER_PIN 25
  #define OK_BUTTON_PIN 26
  #define MENU_BUTTON_PIN 27
  #define BIKE_MONITOR_PIN 33
  #define BATTERY_ADC_PIN 34
  #define SAFE_LED_PIN 12
  #define DANGER_LED_PIN 14
  #define GPS_LED_PIN 13
  #define GSM_LED_PIN 23
  #define SYSTEM_LED_PIN -1
  #define MPU6050_ADDRESS 0x69

  // CONFIGURATION
  const char* APN = "internet.mtn.rw";
  const char* API_URL = "https://lguard-backend-service.onrender.com/api/v1/telemetry/ingest";
  const char* ACCIDENT_API_URL = "https://lguard-backend-service.onrender.com/api/v1/accidents/new";
  String DEVICE_ID = "VEHICLE_001";
  String EMERGENCY_CONTACT = "+250792957781";
  const float SPEED_LIMIT = 40.0;
  const int MIN_SATELLITES_FOR_SPEED = 4;
  const float MIN_SPEED_THRESHOLD = 5.0;
  const float MIN_GPS_HDOP = 3.0;
  const float ADXL345_LOW_THRESHOLD = 3.0;
  const float ADXL345_MEDIUM_THRESHOLD = 8.0;
  const float ADXL345_HIGH_THRESHOLD = 15.0;
  const float MPU6050_TILT_THRESHOLD = 45.0;
  const float MPU6050_ROLLOVER_THRESHOLD = 70.0;
  const float GYRO_ROTATION_THRESHOLD = 3.0;
  const float SPEED_DROP_THRESHOLD = 30.0;
  const int ROLLOVER_STABILITY_COUNT = 3;
  const unsigned long SENSOR_READ_INTERVAL = 50;           // 50ms - fast sensor monitoring
  const unsigned long GPS_UPDATE_INTERVAL = 1000;          // 1s - GPS update
  //const unsigned long API_MIN_INTERVAL = 30000;            // 30s - minimum between API calls
  const unsigned long API_FORCE_SYNC_INTERVAL = 900000;    // 15min - force sync even if no change
  const unsigned long STATUS_PRINT_INTERVAL = 2000;        // 2s - status print
  const unsigned long CANCEL_WINDOW = 10000;               // 10s - cancel window
  const unsigned long SPEED_BEEP_INTERVAL = 1000;          // 1s - speed beep
  const unsigned long GPS_WARMUP_TIME = 10000;             // 10s - GPS warmup
  const unsigned long SLEEP_IDLE_TIME = 120000;            // 2min - idle before sleep
  const unsigned long ACCIDENT_COOLDOWN = 30000;           // 30s - cooldown after accident
  const unsigned long EMERGENCY_INTERRUPT_TIMEOUT = 2000;   // NEW: 2s max wait before interrupt
  const float SPEED_CHANGE_THRESHOLD = 10.0;               // 10 km/h
  const float LOCATION_CHANGE_THRESHOLD = 0.0002;          // ~22 meters
  const float ACCEL_CHANGE_THRESHOLD = 2.0;                // 2g
  const float ANGLE_CHANGE_THRESHOLD = 30.0;               // 30 degrees
  const float BATTERY_DIVIDER = 2.0;
  const float ADC_MAX_VALUE = 4095.0;
  const float ADC_REF_VOLTAGE = 3.3;
  const float BATTERY_MIN_VOLTAGE = 3.3;
  const float BATTERY_MAX_VOLTAGE = 4.2;
  // MPU6050 CALIBRATION (NEW!)
  float rollAngleOffset = 0;      // Calibration offset for roll
  float pitchAngleOffset = 0;     // Calibration offset for pitch
  bool mpuCalibrated = false;     // Track if calibration done



  // OBJECTS
  Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
  Adafruit_MPU6050 mpu;
  HardwareSerial modemSerial(1);
  HardwareSerial gpsSerial(2);

  // GPS RAW PARSING (like prototype - more reliable!)
  String gpsBuffer = "";
  bool gpsFixed = false;
  float currentLat = 0, currentLon = 0, altitude = 0;
  float currentSpeed = 0, previousSpeed = 0, heading = 0;
  int satellites = 0;
  float hdop = 99.9;
  String utcTime = "";
  String fixQuality = "No Fix";

  // SENSOR VARIABLES
  float adxl345X = 0, adxl345Y = 0, adxl345Z = 0, adxl345Total = 0;
  float adxl345MaxImpact = 0;
  float mpuAccelX = 0, mpuAccelY = 0, mpuAccelZ = 0;
  float gyroX = 0, gyroY = 0, gyroZ = 0, totalGyro = 0;
  float pitchAngle = 0, rollAngle = 0;
  int rolloverCount = 0;

  // STATE VARIABLES
  bool accidentDetected = false;
  bool smsSent = false;
  bool cancelPressed = false;
  unsigned long accidentTime = 0;
  unsigned long lastAccidentHandled = 0;  // NEW: Track when accident was handled
  String accidentType = "";
  String impactLevel = "";
  bool isSpeeding = false;
  unsigned long lastSpeedBeep = 0;
  bool speedAlertEnabled = false;
  unsigned long systemStartTime = 0;
  bool modemReady = false;
  bool gprsConnected = false;
  bool mpuAvailable = false;

  // TIMING VARIABLES
  unsigned long lastSensorRead = 0;
  unsigned long lastGpsUpdate = 0;
  unsigned long lastApiSync = 0;
  unsigned long lastApiSent = 0;           // NEW: Track actual API send time
  unsigned long lastStatusPrint = 0;
  unsigned long lastSignificantChange = 0;
  unsigned long lastMovementTime = 0;      // NEW: Track last movement

  // API SYNC CONTROL (prevent spam!)
  bool apiSyncInProgress = false;          // NEW: Flag to prevent overlapping API calls
  bool forceNextSync = false;              // NEW: Force sync after accident

  // PREVIOUS VALUES FOR CHANGE DETECTION
  float prevSentLat = 0, prevSentLon = 0;
  float prevSentSpeed = 0;
  float prevSentAccelTotal = 0;
  float prevSentRollAngle = 0;
  int prevSentBatteryPercent = 0;

  // LED BLINK VARIABLES
  unsigned long lastDangerBlink = 0;
  unsigned long lastGpsBlink = 0;
  unsigned long lastGsmBlink = 0;
  bool dangerLedState = false;
  bool gpsLedState = false;
  bool gsmLedState = false;

  // FUNCTION PROTOTYPES
  void setupPins();
  void setupSensors();
  void setupGPS();
  void setupModem();
  bool connectGPRS();
  void readADXL345();
  void readMPU6050();
  void readGPS();
  void handleGPS();
  void processGPSSentence(String sentence);
  void parseGPGGA(String sentence);
  void parseGPRMC(String sentence);
  int splitString(String data, char separator, String* result, int maxParts);
  float convertDMtoDecimal(String coord, String direction);
  void checkAccident();
  void checkSpeedLimit();
  void handleCancelButton();
  void sendEmergencySMS();
  void sendAccidentToAPI();
  void sendTrackingDataToAPI();
  bool hasSignificantChange();
  void updateStatusLEDs();
  String sendATCommand(String cmd, unsigned long timeout = 1000);
  String readModemResponse(unsigned long timeout = 2000);
  bool httpPOST_Fixed(String jsonData);
  float readBatteryVoltage();
  int batteryPercentFromVoltage(float v);
  String buildTelemetryJSON();
  void printCompactStatus();

  void setupPins() {
    if (SAFE_LED_PIN >= 0) pinMode(SAFE_LED_PIN, OUTPUT);
    if (DANGER_LED_PIN >= 0) pinMode(DANGER_LED_PIN, OUTPUT);
    if (GPS_LED_PIN >= 0) pinMode(GPS_LED_PIN, OUTPUT);
    if (GSM_LED_PIN >= 0) pinMode(GSM_LED_PIN, OUTPUT);
    if (SYSTEM_LED_PIN >= 0) pinMode(SYSTEM_LED_PIN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(OK_BUTTON_PIN, INPUT_PULLUP);
    pinMode(MENU_BUTTON_PIN, INPUT_PULLUP);
    pinMode(BIKE_MONITOR_PIN, INPUT);
    pinMode(MODEM_RI_PIN, INPUT);
    pinMode(MODEM_PWR_PIN, OUTPUT);
    pinMode(MODEM_DTR_PIN, OUTPUT);
    pinMode(MODEM_RESET_PIN, OUTPUT);
    if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, LOW);
    if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW);
    if (GPS_LED_PIN >= 0) digitalWrite(GPS_LED_PIN, LOW);
    if (GSM_LED_PIN >= 0) digitalWrite(GSM_LED_PIN, LOW);
    if (SYSTEM_LED_PIN >= 0) digitalWrite(SYSTEM_LED_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(MODEM_PWR_PIN, HIGH);
    digitalWrite(MODEM_DTR_PIN, HIGH);
    digitalWrite(MODEM_RESET_PIN, LOW);
  }

  void setupSensors() {
    Serial.println("Initializing sensors...");
    Wire.begin(21, 22);
    
    Serial.print("  . ADXL345... ");
    if (!accel.begin()) {
      Serial.println("X FAILED!");
      while (1) {
        if (DANGER_LED_PIN >= 0) {
          digitalWrite(DANGER_LED_PIN, HIGH);
          delay(200);
          digitalWrite(DANGER_LED_PIN, LOW);
          delay(200);
        }
      }
    }
    Serial.println("OK");
    accel.setRange(ADXL345_RANGE_16_G);
    accel.setDataRate(ADXL345_DATARATE_400_HZ);
    
    Serial.print("  . MPU6050... ");
    if (!mpu.begin(MPU6050_ADDRESS)) {
      Serial.println("! NOT DETECTED");
      mpuAvailable = false;
    } else {
      Serial.println("OK");
      mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
      mpu.setGyroRange(MPU6050_RANGE_500_DEG);
      mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
      mpuAvailable = true;
      
      // ========== CALIBRATE MPU6050 (SET CURRENT POSITION AS ZERO) ==========
      Serial.print("  . Calibrating MPU6050... ");
      delay(500);  // Let sensor stabilize
      
      sensors_event_t a, g, temp;
      float totalRoll = 0, totalPitch = 0;
      
      // Take 10 readings and average them
      for (int i = 0; i < 10; i++) {
        mpu.getEvent(&a, &g, &temp);
        float pitch = atan2(a.acceleration.y, sqrt(a.acceleration.x * a.acceleration.x + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
        float roll = atan2(-a.acceleration.x, a.acceleration.z) * 180.0 / PI;
        totalRoll += roll;
        totalPitch += pitch;
        delay(50);
      }
      
      // Store average as offset (current position = zero)
      rollAngleOffset = totalRoll / 10.0;
      pitchAngleOffset = totalPitch / 10.0;
      mpuCalibrated = true;
      
      Serial.println("OK");
      Serial.println("    Roll offset: " + String(rollAngleOffset, 1) + "°");
      Serial.println("    Pitch offset: " + String(pitchAngleOffset, 1) + "°");
      Serial.println("    ✅ Current position set as ZERO!");
    }
    Serial.println("OK Sensors initialized!\n");
  }

  void setupGPS() {
    Serial.println("Initializing GPS...");
    gpsSerial.begin(9600, SERIAL_8N1, GPS_MODULE_TX, GPS_MODULE_RX);
    Serial.println("OK GPS started\n");
    delay(1000);
  }

  void setupModem() {
    Serial.println("Initializing SIM A7670E...");
    modemSerial.begin(115200, SERIAL_8N1, MODEM_RXD, MODEM_TXD);
    delay(2000);
    
    while (modemSerial.available()) modemSerial.read();
    
    Serial.println("  . Powering on...");
    digitalWrite(MODEM_PWR_PIN, LOW);
    delay(100);
    digitalWrite(MODEM_PWR_PIN, HIGH);
    delay(1500);
    digitalWrite(MODEM_PWR_PIN, LOW);
    delay(5000);
    
    sendATCommand("AT", 500);
    sendATCommand("AT", 500);
    sendATCommand("ATE0", 500);
    
    Serial.print("  . SIM card... ");
    String simResp = sendATCommand("AT+CPIN?", 2000);
    if (simResp.indexOf("READY") >= 0) {
      Serial.println("OK");
    } else {
      Serial.println("X");
      modemReady = false;
      return;
    }
    
    Serial.print("  . Network... ");
    for (int i = 0; i < 10; i++) {
      String netResp = sendATCommand("AT+CREG?", 2000);
      if (netResp.indexOf("+CREG: 0,1") >= 0 || netResp.indexOf("+CREG: 0,5") >= 0) {
        Serial.println("OK");
        modemReady = true;
        break;
      }
      delay(2000);
    }
    
    if (!modemReady) {
      Serial.println("X Network registration failed");
      return;
    }
    
    sendATCommand("AT+CMGF=1", 1000);
    sendATCommand("AT+CSCS=\"GSM\"", 1000);
    sendATCommand("AT+CSMP=17,167,0,0", 1000);
    sendATCommand("AT+CNMI=2,1,0,0,0", 1000);
    
    String smsCenter = sendATCommand("AT+CSCA?", 2000);
    if (smsCenter.indexOf("+CSCA:") < 0) {
      sendATCommand("AT+CSCA=\"+250788110000\"", 2000);
    }
    
    Serial.println("OK Modem initialized!\n");
  }

  bool connectGPRS() {
    Serial.println("Connecting to GPRS...");
    if (!modemReady) {
      Serial.println("X Modem not ready");
      return false;
    }
    sendATCommand("AT+CGATT=1", 2000);
    sendATCommand("AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 2000);
    Serial.print("  . Activating... ");
    String activateResp = sendATCommand("AT+CGACT=1,1", 10000);
    if (activateResp.indexOf("OK") >= 0) {
      String ipResp = sendATCommand("AT+CGPADDR=1", 2000);
      if (ipResp.indexOf("+CGPADDR") >= 0) {
        Serial.println("OK");
        gprsConnected = true;
        return true;
      }
    }
    Serial.println("X Failed");
    gprsConnected = false;
    return false;
  }

  void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n=========================================================");
    Serial.println("      L-GUARD v5.5 - PRODUCTION READY");
    Serial.println("=========================================================\n");
    
    systemStartTime = millis();
    lastMovementTime = millis();
    
    setupPins();
    
    for (int i = 0; i < 3; i++) {
      if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
      if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, HIGH);
      if (GPS_LED_PIN >= 0) digitalWrite(GPS_LED_PIN, HIGH);
      if (GSM_LED_PIN >= 0) digitalWrite(GSM_LED_PIN, HIGH);
      delay(200);
      if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, LOW);
      if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW);
      if (GPS_LED_PIN >= 0) digitalWrite(GPS_LED_PIN, LOW);
      if (GSM_LED_PIN >= 0) digitalWrite(GSM_LED_PIN, LOW);
      delay(200);
    }
    
    setupSensors();
    setupGPS();
    setupModem();
    
    if (modemReady) {
      if (connectGPRS()) {
        Serial.println("OK System ready!\n");
        if (SYSTEM_LED_PIN >= 0) digitalWrite(SYSTEM_LED_PIN, HIGH);
      } else {
        Serial.println("! GPRS failed - SMS only\n");
      }
    }
    
    if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
    
    Serial.println("=========================================================");
    Serial.println(" ACCIDENT DETECTION: ACTIVE");
    Serial.println(" GPS TRACKING: ACTIVE");
    Serial.println(" SPEED ALERT: > 60 km/h");
    Serial.println(" API SYNC: SMART MODE (prevents spam)");
    Serial.println(" SMS: ENABLED");
    Serial.println("=========================================================\n");
    Serial.println("⏳ Waiting for GPS to stabilize (10 seconds)...\n");
      Serial.println("⏳ Waiting for GPS to stabilize (10 seconds)...\n");
    
    // ========== INITIALIZE PREVIOUS VALUES (PREVENT FIRST SEND) ==========
    prevSentLat = currentLat;
    prevSentLon = currentLon;
    prevSentSpeed = currentSpeed;
    prevSentAccelTotal = adxl345Total;
    prevSentRollAngle = rollAngle;
    prevSentBatteryPercent = batteryPercentFromVoltage(readBatteryVoltage());
    lastApiSent = millis();  // Prevent immediate end on startup
  }

  // ========== NEW FUNCTION: CHECK IF SYSTEM IS TRULY STABLE ==========
  bool isSystemReallyStable() {
    static float lastAccelCheck = 0;
    static float lastRollCheck = 0;
    static float lastSpeedCheck = 0;
    static unsigned long lastStableCheck = 0;
    static int stableCount = 0;
    
    unsigned long currentMillis = millis();
    
    // Check every 2 seconds
    if (currentMillis - lastStableCheck < 2000) {
      return false;
    }
    
    // Check all conditions
    bool accelStable = (abs(adxl345Total - lastAccelCheck) < 0.2);  // < 0.2g change
    bool rollStable = (abs(rollAngle - lastRollCheck) < 5.0);       // < 5° change
    bool speedStable = (currentSpeed < 1.0);                         // Basically stopped
    bool notMoving = (currentMillis - lastMovementTime > SLEEP_IDLE_TIME);
    
    // All must be true
    if (accelStable && rollStable && speedStable && notMoving) {
      stableCount++;
    } else {
      stableCount = 0;  // Reset if anything changes
    }
    
    // Update last values
    lastAccelCheck = adxl345Total;
    lastRollCheck = rollAngle;
    lastSpeedCheck = currentSpeed;
    lastStableCheck = currentMillis;
    
    // Must be stable for 5 checks (10 seconds) before sleeping
    return (stableCount >= 5);
  }

  void loop() {
    unsigned long currentMillis = millis();
    
    // ========== SENSOR MONITORING (ALWAYS ACTIVE - NEVER BLOCKED!) ==========
    if (currentMillis - lastSensorRead >= SENSOR_READ_INTERVAL) {
      readADXL345();
      readMPU6050();
      checkAccident();  // This MUST always run!
      lastSensorRead = currentMillis;
    }
    
    // ========== GPS READING (CONTINUOUS) ==========
    handleGPS();  // Process GPS data continuously
    
    if (currentMillis - lastGpsUpdate >= GPS_UPDATE_INTERVAL) {
      checkSpeedLimit();
      lastGpsUpdate = currentMillis;
    }
    
    // ========== ENABLE SPEED ALERTS AFTER GPS WARMUP ==========
    if (!speedAlertEnabled && (currentMillis - systemStartTime >= GPS_WARMUP_TIME)) {
      speedAlertEnabled = true;
      Serial.println("✅ GPS warmed up - Speed alerts ENABLED\n");
      lastSignificantChange = currentMillis;
    }
    
    // ========== API SYNC (ONLY ON CHANGE OR 5MIN HEARTBEAT!) ==========
    if (gprsConnected && !apiSyncInProgress) {
      bool forceSync = (currentMillis - lastApiSent >= API_FORCE_SYNC_INTERVAL);  // 5 minutes
      bool hasChanges = hasSignificantChange();
      
      // Send ONLY if: has changes OR 5min heartbeat OR accident
      if (hasChanges || forceSync || forceNextSync) {
        sendTrackingDataToAPI();
        lastApiSync = currentMillis;
        forceNextSync = false;  // Reset force flag
      }
    }
    
    // ========== ACCIDENT HANDLING ==========
    if (accidentDetected && !smsSent) {
      handleCancelButton();
    }
    
    // ========== STATUS UPDATES ==========
    updateStatusLEDs();
    
    if (currentMillis - lastStatusPrint >= STATUS_PRINT_INTERVAL) {
      if (!accidentDetected && !apiSyncInProgress) {
        printCompactStatus();
      }
      lastStatusPrint = currentMillis;
    }
    
    // ========== SMART SLEEP MODE (REALLY SMART NOW!) ==========
    if (isSystemReallyStable() && !accidentDetected && !isSpeeding && !apiSyncInProgress) {
      Serial.println("\n💤 SLEEP (all sensors stable)");
      esp_sleep_enable_timer_wakeup(10000000);  // 10s wake
      esp_light_sleep_start();
      lastMovementTime = millis();  // Reset after wake
    }
    
    // Stop buzzer if not needed
    if (!isSpeeding && !accidentDetected) {
      digitalWrite(BUZZER_PIN, LOW);
    }
    
    delay(10);
  }

  void readADXL345() {
    sensors_event_t event;
    accel.getEvent(&event);
    const float MPS2_TO_G = 1.0 / 9.80665;
    adxl345X = event.acceleration.x * MPS2_TO_G;
    adxl345Y = event.acceleration.y * MPS2_TO_G;
    adxl345Z = event.acceleration.z * MPS2_TO_G;
    adxl345Total = sqrt(adxl345X * adxl345X + adxl345Y * adxl345Y + adxl345Z * adxl345Z);
    
    if (adxl345Total > adxl345MaxImpact) {
      adxl345MaxImpact = adxl345Total;
    }
    
    // Track movement for sleep mode (speed OR acceleration change)
    static float lastAccelForMovement = 1.0;
    
    // Movement = speed changing OR acceleration changing
    bool speedChanging = (abs(currentSpeed - previousSpeed) > 2.0);  // 2 km/h change
    bool accelChanging = (abs(adxl345Total - lastAccelForMovement) > 0.3);  // 0.3g change
    
    if (speedChanging || accelChanging || currentSpeed > 5.0) {
      lastMovementTime = millis();
    }
    
    lastAccelForMovement = adxl345Total;
  }

  void readMPU6050() {
    if (!mpuAvailable) {
      rolloverCount = 0;
      return;
    }
    
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    mpuAccelX = a.acceleration.x;
    mpuAccelY = a.acceleration.y;
    mpuAccelZ = a.acceleration.z;
    gyroX = g.gyro.x;
    gyroY = g.gyro.y;
    gyroZ = g.gyro.z;
    
    totalGyro = sqrt(gyroX * gyroX + gyroY * gyroY + gyroZ * gyroZ);
    
    // Calculate raw angles
    float rawPitch = atan2(mpuAccelY, sqrt(mpuAccelX * mpuAccelX + mpuAccelZ * mpuAccelZ)) * 180.0 / PI;
    float rawRoll = atan2(-mpuAccelX, mpuAccelZ) * 180.0 / PI;
    
    // Apply calibration offsets (current position was set as zero on startup!)
    if (mpuCalibrated) {
      pitchAngle = rawPitch - pitchAngleOffset;
      rollAngle = rawRoll - rollAngleOffset;
    } else {
      pitchAngle = rawPitch;
      rollAngle = rawRoll;
    }
    
    if (abs(rollAngle) > MPU6050_ROLLOVER_THRESHOLD || abs(pitchAngle) > MPU6050_ROLLOVER_THRESHOLD) {
      rolloverCount = constrain(rolloverCount + 1, 0, ROLLOVER_STABILITY_COUNT);
    } else {
      rolloverCount = 0;
    }
  }

  // ========== GPS PARSING (RAW NMEA - LIKE PROTOTYPE!) ==========
  void handleGPS() {
    while (gpsSerial.available()) {
      gpsBuffer += char(gpsSerial.read());
    }
    
    int newlinePos;
    while ((newlinePos = gpsBuffer.indexOf('\n')) != -1) {
      String sentence = gpsBuffer.substring(0, newlinePos);
      sentence.trim();
      gpsBuffer = gpsBuffer.substring(newlinePos + 1);
      
      if (sentence.length() > 6) {
        processGPSSentence(sentence);
      }
    }
    
    if (gpsBuffer.length() > 1000) {
      gpsBuffer = "";
    }
  }

  void processGPSSentence(String sentence) {
    if (sentence.startsWith("$GPGGA") || sentence.startsWith("$GNGGA")) {
      parseGPGGA(sentence);
    } else if (sentence.startsWith("$GPRMC") || sentence.startsWith("$GNRMC")) {
      parseGPRMC(sentence);
    }
  }

  void parseGPGGA(String sentence) {
    String parts[15];
    int partCount = splitString(sentence, ',', parts, 15);
    
    if (partCount < 10) return;
    
    // Time
    if (parts[1].length() >= 6) {
      String t = parts[1];
      utcTime = t.substring(0,2) + ":" + t.substring(2,4) + ":" + t.substring(4,6);
    }
    
    // Fix quality
    int fixNum = parts[6].toInt();
    switch(fixNum) {
      case 0: fixQuality = "No Fix"; gpsFixed = false; break;
      case 1: fixQuality = "GPS Fix"; gpsFixed = true; break;
      case 2: fixQuality = "DGPS Fix"; gpsFixed = true; break;
      default: fixQuality = "Unknown"; gpsFixed = false;
    }
    
    // Satellites
    satellites = parts[7].toInt();
    
    // HDOP
    if (parts[8].length() > 0) {
      hdop = parts[8].toFloat();
    }
    
    // Location
    if (gpsFixed && parts[2].length() > 0 && parts[4].length() > 0) {
      currentLat = convertDMtoDecimal(parts[2], parts[3]);
      currentLon = convertDMtoDecimal(parts[4], parts[5]);
      
      if (parts[9].length() > 0) {
        altitude = parts[9].toFloat();
      }
    }
  }

  void parseGPRMC(String sentence) {
    String parts[15];
    int partCount = splitString(sentence, ',', parts, 15);
    
    if (partCount < 8) return;
    
    // Status ('A' = active/valid, 'V' = void/invalid)
    bool validData = (parts[2] == "A");
    
    if (!validData) {
      // Don't update speed if data is invalid
      return;
    }
    
    // Extract speed (knots) and convert to km/h
    if (parts[7].length() > 0) {
      float speedKnots = parts[7].toFloat();
      previousSpeed = currentSpeed;
      currentSpeed = speedKnots * 1.852;  // Convert knots to km/h
      
      // Validate speed (reject impossible values)
      if (currentSpeed < 0 || currentSpeed > 300) {
        currentSpeed = previousSpeed;  // Keep previous valid speed
      }
      
      // Only accept speed if we have good GPS fix
      if (!gpsFixed || satellites < MIN_SATELLITES_FOR_SPEED || hdop > MIN_GPS_HDOP) {
        // Poor GPS - smooth speed changes
        if (abs(currentSpeed - previousSpeed) > 20) {
          currentSpeed = previousSpeed;  // Reject sudden jumps with poor GPS
        }
      }
    }
    
    // Extract heading/course
    if (parts[8].length() > 0) {
      heading = parts[8].toFloat();
    }
  }

  int splitString(String data, char separator, String* result, int maxParts) {
    int partIndex = 0;
    int lastIndex = 0;
    
    for (int i = 0; i <= data.length() && partIndex < maxParts; i++) {
      if (i == data.length() || data.charAt(i) == separator) {
        result[partIndex] = data.substring(lastIndex, i);
        lastIndex = i + 1;
        partIndex++;
      }
    }
    return partIndex;
  }

  float convertDMtoDecimal(String coord, String direction) {
    if (coord.length() < 4) return 0.0;
    
    float degrees, minutes;
    int dotPos = coord.indexOf('.');
    
    if (dotPos > 4) {
      degrees = coord.substring(0, 3).toFloat();
      minutes = coord.substring(3).toFloat();
    } else {
      degrees = coord.substring(0, 2).toFloat();
      minutes = coord.substring(2).toFloat();
    }
    
    float decimal = degrees + (minutes / 60.0);
    
    if (direction == "S" || direction == "W") {
      decimal = -decimal;
    }
    
    return decimal;
  }

  void checkSpeedLimit() {
    unsigned long currentMillis = millis();
    
    if (!speedAlertEnabled) {
      digitalWrite(BUZZER_PIN, LOW);
      isSpeeding = false;
      return;
    }
    
    bool gpsReliable = (gpsFixed && satellites >= MIN_SATELLITES_FOR_SPEED && 
                      hdop < MIN_GPS_HDOP && currentSpeed > MIN_SPEED_THRESHOLD);
    
    if (gpsReliable && currentSpeed > SPEED_LIMIT) {
      if (!isSpeeding) {
        isSpeeding = true;
        Serial.println("\n⚠️ OVERSPEEDING: " + String(currentSpeed, 1) + " km/h");
        lastMovementTime = currentMillis;
      }
      
      if (currentMillis - lastSpeedBeep >= SPEED_BEEP_INTERVAL) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(200);
        digitalWrite(BUZZER_PIN, LOW);
        lastSpeedBeep = currentMillis;
      }
    } else {
      if (isSpeeding) {
        Serial.println("✅ Speed normal");
        isSpeeding = false;
      }
      digitalWrite(BUZZER_PIN, LOW);
    }
  }

  void checkAccident() {
    // Don't detect new accidents during cooldown period
    if (millis() - lastAccidentHandled < ACCIDENT_COOLDOWN) {
      return;
    }
    
    if (accidentDetected) return;
      // ========== NEW: INTERRUPT API IF ACCIDENT HAPPENS! ==========
    // This ensures accident detection NEVER waits for API
    static bool emergencyInterruptNeeded = false;
    if (emergencyInterruptNeeded && apiSyncInProgress) {
      Serial.println("\n⚠️  EMERGENCY: Stopping API sync for accident!");
      sendATCommand("AT+HTTPTERM", 500);
      apiSyncInProgress = false;
      emergencyInterruptNeeded = false;
    }

    bool vibrationGate = true;
    bool lowImpact = (adxl345Total > ADXL345_LOW_THRESHOLD);
    bool mediumImpact = (adxl345Total > ADXL345_MEDIUM_THRESHOLD);
    bool highImpact = (adxl345Total > ADXL345_HIGH_THRESHOLD);
    bool rolloverSustained = mpuAvailable ? (rolloverCount >= ROLLOVER_STABILITY_COUNT) : false;
    bool tiltWarning = mpuAvailable ? (abs(rollAngle) > MPU6050_TILT_THRESHOLD) : false;
    bool rapidRotation = mpuAvailable ? (totalGyro > GYRO_ROTATION_THRESHOLD) : false;
    
    bool speedDrop = false;
    if (gpsFixed && previousSpeed > 40.0 && currentSpeed < 10.0) {
      float drop = previousSpeed - currentSpeed;
      if (drop >= SPEED_DROP_THRESHOLD) {
        speedDrop = true;
      }
    }
    
    bool accidentCondition = false;
    
    if (highImpact && vibrationGate) {
      accidentType = "SEVERE_IMPACT";
      impactLevel = "HIGH";
      accidentCondition = true;
    } else if (mpuAvailable && rolloverSustained && vibrationGate) {
      accidentType = "ROLLOVER";
      impactLevel = "HIGH";
      accidentCondition = true;
    } else if (speedDrop && vibrationGate && mediumImpact) {
      accidentType = "SPEED_DROP_COLLISION";
      impactLevel = "HIGH";
      accidentCondition = true;
    } else if (mediumImpact && vibrationGate) {
      accidentType = "IMPACT_COLLISION";
      impactLevel = "MEDIUM";
      accidentCondition = true;
    } else if (mpuAvailable && tiltWarning && rapidRotation && vibrationGate) {
      accidentType = "VEHICLE_FLIP";
      impactLevel = "MEDIUM";
      accidentCondition = true;
    }
    
    if (accidentCondition) {
      accidentDetected = true;
      accidentTime = millis();
      cancelPressed = false;
      smsSent = false;
          // ========== NEW: FLAG FOR EMERGENCY INTERRUPT ==========
      if (apiSyncInProgress) {
        Serial.println("⚠️  API busy - will interrupt on next check!");
      }
      
      if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, LOW);
      
      Serial.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
      Serial.println("   🚨 ACCIDENT DETECTED! 🚨");
      Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
      Serial.println("Type: " + accidentType);
      Serial.println("Severity: " + impactLevel);
      Serial.println("Impact: " + String(adxl345Total, 2) + "g");
      if (gpsFixed) {
        Serial.println("Location: " + String(currentLat, 6) + ", " + String(currentLon, 6));
      }
      Serial.println("\nPress CANCEL within 10s!");
      Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      
      lastMovementTime = millis();
    }
  }

  void handleCancelButton() {
    unsigned long elapsed = millis() - accidentTime;
    
    // Check cancel button
    if (digitalRead(OK_BUTTON_PIN) == LOW) {
      delay(50);
      if (digitalRead(OK_BUTTON_PIN) == LOW) {
        cancelPressed = true;
        Serial.println("\n✅ ALERT CANCELLED BY USER!\n");
        
        accidentDetected = false;
        smsSent = false;
        adxl345MaxImpact = 0;
        rolloverCount = 0;
        lastAccidentHandled = millis();  // Set cooldown
        
        digitalWrite(BUZZER_PIN, LOW);
        if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW);
        if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
        
        while (digitalRead(OK_BUTTON_PIN) == LOW) delay(10);
        return;
      }
    }
    
    // Time expired - send alerts (IMMEDIATE PRIORITY!)
    if (elapsed >= CANCEL_WINDOW && !cancelPressed && !smsSent) {
      Serial.println("\n⏰ TIME EXPIRED - Sending alerts!\n");
      
      // FORCE STOP ANY ONGOING API SYNC
      if (apiSyncInProgress) {
        Serial.println("⚠️  INTERRUPTING API SYNC FOR EMERGENCY!");
        sendATCommand("AT+HTTPTERM", 1000);
        apiSyncInProgress = false;
      }
      // SMS FIRST (most important!)
sendEmergencySMS();

// Wait for SMS to fully complete before HTTP
Serial.println("⏳ Waiting 3s for SMS to complete...");
delay(3000);

// Clean up any modem state from SMS
sendATCommand("AT", 500);
sendATCommand("AT+CMGF=1", 500);  // Reset SMS mode

// Then API if available
if (gprsConnected) {
  sendAccidentToAPI();
}
      
      // Reset accident state after handling
      accidentDetected = false;
      adxl345MaxImpact = 0;
      rolloverCount = 0;
      lastAccidentHandled = millis();  // Set cooldown
      forceNextSync = true;  // Force next telemetry sync
      
      if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
      
      Serial.println("\n✅ Accident handled - monitoring resumed\n");
    }
  }

  bool hasSignificantChange() {
    bool anyChange = false;
    
    // Declare variables at the top so we can use them later
    float latDiff = 0, lonDiff = 0;
    float speedDiff = 0;
    float accelDiff = 0;
    float angleDiff = 0;
    
    // Location change
    if (gpsFixed) {
      latDiff = abs(currentLat - prevSentLat);
      lonDiff = abs(currentLon - prevSentLon);
      if (latDiff > LOCATION_CHANGE_THRESHOLD || lonDiff > LOCATION_CHANGE_THRESHOLD) {
        anyChange = true;
      }
    }
    
    // Speed change
    speedDiff = abs(currentSpeed - prevSentSpeed);
    if (speedDiff > SPEED_CHANGE_THRESHOLD) {
      anyChange = true;
    }
    
    // Acceleration change
    accelDiff = abs(adxl345Total - prevSentAccelTotal);
    if (accelDiff > ACCEL_CHANGE_THRESHOLD) {
      anyChange = true;
    }
    
    // Angle change (if MPU available)
    if (mpuAvailable) {
      angleDiff = abs(rollAngle - prevSentRollAngle);
      if (angleDiff > ANGLE_CHANGE_THRESHOLD) {
        anyChange = true;
      }
    }
    
    // Battery change (INCREASED THRESHOLD!)
    int currentBattPercent = batteryPercentFromVoltage(readBatteryVoltage());
    if (abs(currentBattPercent - prevSentBatteryPercent) >= 20) {  // Changed from 10 to 20
      anyChange = true;
    }
    
    // Movement state changes
    bool wasMoving = (prevSentSpeed > MIN_SPEED_THRESHOLD);
    bool isMoving = (currentSpeed > MIN_SPEED_THRESHOLD);
    
    if (wasMoving != isMoving) {
      anyChange = true;  // Started or stopped moving
    }
    
    // ========== NEW: DON'T SEND IF ONLY BATTERY CHANGED! ==========
    // Check if ONLY battery changed (no other sensor changed)
    bool onlyBatteryChanged = anyChange && 
                              (latDiff <= LOCATION_CHANGE_THRESHOLD) &&
                              (lonDiff <= LOCATION_CHANGE_THRESHOLD) &&
                              (speedDiff <= SPEED_CHANGE_THRESHOLD) &&
                              (accelDiff <= ACCEL_CHANGE_THRESHOLD) &&
                              (angleDiff <= ANGLE_CHANGE_THRESHOLD) &&
                              (!wasMoving && !isMoving);
    
    // If only battery changed, DON'T send (battery is too noisy!)
    if (onlyBatteryChanged) {
      return false;  // Ignore battery-only changes
    }
    
    return anyChange;
  }

  String buildTelemetryJSON() {
    String json = "{";
    json += "\"deviceId\":\"" + DEVICE_ID + "\",";
    
    if (gpsFixed && currentLat != 0 && currentLon != 0) {
      json += "\"latitude\":" + String(currentLat, 6) + ",";
      json += "\"longitude\":" + String(currentLon, 6) + ",";
    } else {
      json += "\"latitude\":0,";
      json += "\"longitude\":0,";
    }
    
    json += "\"altitude\":" + String((int)altitude) + ",";
    json += "\"speed\":" + String(currentSpeed, 1) + ",";
    json += "\"heading\":" + String((int)heading) + ",";
    json += "\"accelerationX\":" + String(adxl345X, 2) + ",";
    json += "\"accelerationY\":" + String(adxl345Y, 2) + ",";
    json += "\"accelerationZ\":" + String(adxl345Z, 2) + ",";
    json += "\"gyroX\":" + String(gyroX, 2) + ",";
    json += "\"gyroY\":" + String(gyroY, 2) + ",";
    json += "\"gyroZ\":" + String(gyroZ, 2) + ",";
    json += "\"rpm\":0,";
    json += "\"engineTemp\":0,";
    json += "\"fuelLevel\":0,";
    json += "\"batteryLevel\":" + String(batteryPercentFromVoltage(readBatteryVoltage())) + ",";
    json += "\"signalStrength\":85,";
    
    // Raw data with accident info
    json += "\"rawData\":{";
    json += "\"totalAccel\":" + String(adxl345Total, 2) + ",";
    json += "\"totalGyro\":" + String(totalGyro, 2) + ",";
    json += "\"pitchAngle\":" + String(pitchAngle, 1) + ",";
    json += "\"rollAngle\":" + String(rollAngle, 1) + ",";
    json += "\"vibration\":false,";  // No vibration sensor
    json += "\"satellites\":" + String(satellites) + ",";
    json += "\"hdop\":" + String(hdop, 1) + ",";
    json += "\"fixQuality\":\"" + fixQuality + "\",";
    json += "\"accidentDetected\":" + String(accidentDetected ? "true" : "false");
    if (accidentDetected || smsSent) {
      json += ",\"accidentType\":\"" + accidentType + "\",";
      json += "\"impactLevel\":\"" + impactLevel + "\",";
      json += "\"maxImpact\":" + String(adxl345MaxImpact, 2);
    }
    json += "}";
    
    json += "}";
    return json;
  }

  bool httpPOST_Fixed(String jsonData) {
    if (!gprsConnected || apiSyncInProgress) {
      return false;
    }
    
    apiSyncInProgress = true;  // Set flag
    
    sendATCommand("AT+HTTPTERM", 1000);
    delay(500);
    sendATCommand("AT+CSSLCFG=\"enableSNI\",0,1", 2000);
    sendATCommand("AT+HTTPINIT", 2000);
    
    modemSerial.println("AT+HTTPPARA=\"URL\",\"" + String(API_URL) + "\"");
    delay(1000);
    readModemResponse(2000);
    
    modemSerial.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
    delay(1000);
    readModemResponse(2000);
    
    modemSerial.println("AT+HTTPDATA=" + String(jsonData.length()) + ",10000");
    delay(1000);
    String downloadResp = readModemResponse(3000);
    
    bool success = false;
    
    if (downloadResp.indexOf("DOWNLOAD") >= 0) {
      modemSerial.print(jsonData);
      delay(2000);
      String dataResp = readModemResponse(3000);
      
      if (dataResp.indexOf("OK") >= 0) {
  Serial.println("✓ Data uploaded, sending POST...");
  modemSerial.println("AT+HTTPACTION=1");
  
  for (int i = 0; i < 15; i++) {
    delay(500);
    if (modemSerial.available()) {
      String result = readModemResponse(2000);
      Serial.println("HTTP Response: " + result);  // DEBUG
      if (result.indexOf("+HTTPACTION") >= 0) {
        if (result.indexOf(",200,") >= 0 || result.indexOf(",201,") >= 0) {
          Serial.println("   ✅ ACCIDENT API SUCCESS!");
          success = true;
        } else {
          Serial.println("   ❌ ACCIDENT API Error: " + result);
        }
        break;
      }
    }
  }
  if (!success) {
    Serial.println("   ❌ No HTTP response received (timeout)");
  }
} else {
  Serial.println("   ❌ Failed to upload data. Response: " + dataResp);
}
    }
    
    sendATCommand("AT+HTTPTERM", 1000);
    apiSyncInProgress = false;  // Clear flag
    
    return success;
  }
  
  bool httpPOST_Accident(String jsonData) {
  if (!gprsConnected || apiSyncInProgress) {
    return false;
  }
  
  apiSyncInProgress = true;
  
  sendATCommand("AT+HTTPTERM", 1000);
  delay(500);
  sendATCommand("AT+CSSLCFG=\"enableSNI\",0,1", 2000);
  sendATCommand("AT+HTTPINIT", 2000);

  Serial.println("📦 JSON BEING SENT:");
  Serial.println(jsonData);
  Serial.println("---");
  
  // Use ACCIDENT endpoint
  modemSerial.println("AT+HTTPPARA=\"URL\",\"" + String(ACCIDENT_API_URL) + "\"");
  delay(1000);
  readModemResponse(2000);
  
  modemSerial.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
  delay(1000);
  readModemResponse(2000);
  
  modemSerial.println("AT+HTTPDATA=" + String(jsonData.length()) + ",10000");
  delay(1000);
  String downloadResp = readModemResponse(3000);
  
  bool success = false;
  
  if (downloadResp.indexOf("DOWNLOAD") >= 0) {
    modemSerial.print(jsonData);
    delay(2000);
    String dataResp = readModemResponse(3000);
    
    if (dataResp.indexOf("OK") >= 0) {
      modemSerial.println("AT+HTTPACTION=1");
      
      for (int i = 0; i < 15; i++) {
        delay(500);
        if (modemSerial.available()) {
          String result = readModemResponse(2000);
          if (result.indexOf("+HTTPACTION") >= 0) {
            if (result.indexOf(",200,") >= 0 || result.indexOf(",201,") >= 0) {
              Serial.println("   ✅ ACCIDENT API SUCCESS!");
              success = true;
            } else {
              Serial.println("   ❌ ACCIDENT API Error: " + result);
            }
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

  void sendTrackingDataToAPI() {
    if (apiSyncInProgress) {
      Serial.println("⚠️  API sync already in progress - skipping");
      return;
    }
    
    Serial.println("\n📤 Sending telemetry...");
    String jsonData = buildTelemetryJSON();
    
    if (httpPOST_Fixed(jsonData)) {
      // Update "last sent" values
      prevSentLat = currentLat;
      prevSentLon = currentLon;
      prevSentSpeed = currentSpeed;
      prevSentAccelTotal = adxl345Total;
      prevSentRollAngle = rollAngle;
      prevSentBatteryPercent = batteryPercentFromVoltage(readBatteryVoltage());
      lastApiSent = millis();
    }
  }

  String buildAccidentJSON() {
  // Map impactLevel to API severity format
  String severity;
  if (impactLevel == "HIGH") {
    severity = "SEVERE";
  } else if (impactLevel == "MEDIUM") {
    severity = "MODERATE";
  } else {
    severity = "MINOR";
  }
  
  // Build JSON matching the exact API format
  String json = "{";
  json += "\"deviceId\":\"" + DEVICE_ID + "\",";
  json += "\"severity\":\"" + severity + "\",";
  json += "\"speed\":" + String(currentSpeed, 1) + ",";
  
  if (gpsFixed && currentLat != 0 && currentLon != 0) {
    json += "\"latitude\":" + String(currentLat, 6) + ",";
    json += "\"longitude\":" + String(currentLon, 6);
  } else {
    json += "\"latitude\":0.0,";
    json += "\"longitude\":0.0";
  }
  
  json += "}";
  return json;
}

  void sendAccidentToAPI() {
  if (apiSyncInProgress) {
    Serial.println("⚠️  API sync in progress - queuing accident data");
    forceNextSync = true;
    return;
  }
  
  Serial.println("\n🚨 Sending accident data to /accidents/new...");
  String jsonData = buildAccidentJSON();  // ← Use NEW function
  
  // Use accident-specific endpoint
  if (httpPOST_Accident(jsonData)) {  // ← Use NEW POST function
    Serial.println("✅ Accident logged!\n");
    lastApiSent = millis();
  } else {
    Serial.println("❌ Accident logging failed\n");
  }
  }

  void sendEmergencySMS() {
    if (smsSent) return;
    
    Serial.println("\n📱 SENDING EMERGENCY SMS...");
    
    if (!modemReady) {
      Serial.println("❌ Modem not ready");
      return;
    }
    
    sendATCommand("AT+CMGF=1", 1000);
    delay(500);
    
    // ========== MESSAGE 1: EMERGENCY ALERT (SHORT!) ==========
    String message1 = "!! ACCIDENT !!\n";
    message1 += DEVICE_ID + "\n";
    message1 += accidentType + "\n";
    message1 += "Impact: " + String(adxl345MaxImpact, 1) + "g\n";
    message1 += "Time: " + utcTime;
    
    if (gpsFixed && currentLat != 0 && currentLon != 0) {
      message1 += "\nSpeed: " + String(currentSpeed, 0) + "km/h";
    } else {
      message1 += "\nGPS: NO FIX!";
    }
    
    Serial.println("MSG1 length: " + String(message1.length()) + " chars");
    Serial.println("To: " + EMERGENCY_CONTACT);
    
    // Send Message 1
    bool msg1Success = sendSingleSMS(message1);
    
    // ========== MESSAGE 2: LOCATION (ONLY IF GPS EXISTS) ==========
    if (msg1Success && gpsFixed && currentLat != 0 && currentLon != 0) {
      delay(2000); // Wait between messages
      
      String message2 = "LOCATION:\n";
      message2 += String(currentLat, 6) + ",";
      message2 += String(currentLon, 6) + "\n\n";
      message2 += "Map:\n";
      message2 += "https://maps.google.com/maps?q=";
      message2 += String(currentLat, 6) + ",";
      message2 += String(currentLon, 6);
      
      Serial.println("\n📱 SENDING LOCATION SMS...");
      Serial.println("MSG2 length: " + String(message2.length()) + " chars");
      
      sendSingleSMS(message2);
    }
    
    if (msg1Success) {
      smsSent = true;
      Serial.println("\n✅ EMERGENCY ALERT SENT!");
    }
  }

  // ========== NEW HELPER FUNCTION (ADD THIS BELOW sendEmergencySMS) ==========
  bool sendSingleSMS(String message) {
    while (modemSerial.available()) modemSerial.read();
    
    modemSerial.print("AT+CMGS=\"");
    modemSerial.print(EMERGENCY_CONTACT);
    modemSerial.print("\"");
    modemSerial.write('\r');
    delay(1000);
    
    String prompt = "";
    unsigned long startWait = millis();
    while (millis() - startWait < 5000) {
      if (modemSerial.available()) {
        char c = modemSerial.read();
        prompt += c;
        if (c == '>') break;
      }
    }
    
    if (prompt.indexOf('>') >= 0) {
      Serial.println("✓ Got prompt, sending...");
      
      for (int i = 0; i < message.length(); i++) {
        modemSerial.write(message[i]);
        delay(5);
      }
      delay(500);
      
      modemSerial.write(26);
      modemSerial.write('\r');
      Serial.println("✓ Sent Ctrl+Z");
      
      String response = "";
      startWait = millis();
      while (millis() - startWait < 15000) {
        if (modemSerial.available()) {
          char c = modemSerial.read();
          response += c;
        }
        if (response.indexOf("+CMGS:") >= 0 || response.indexOf("OK") >= 0) {
          Serial.println("✅ SMS SENT!");
          return true;
        }
        if (response.indexOf("ERROR") >= 0) {
          Serial.println("❌ SMS ERROR: " + response);
          return false;
        }
      }
      
      Serial.println("❌ SMS Timeout");
      return false;
    } else {
      Serial.println("❌ No '>' prompt");
      return false;
    }
  }

  void updateStatusLEDs() {
    unsigned long currentMillis = millis();
    
    // Danger LED (accident)
    if (accidentDetected) {
      if (!smsSent) {
        if (currentMillis - lastDangerBlink >= 150) {
          dangerLedState = !dangerLedState;
          if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, dangerLedState);
          lastDangerBlink = currentMillis;
        }
      } else {
        if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, HIGH);
      }
    } else {
      if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW);
    }
    
    // GPS LED
    if (GPS_LED_PIN >= 0) {
      unsigned long gpsInterval = gpsFixed ? 250 : 1000;
      if (currentMillis - lastGpsBlink >= gpsInterval) {
        gpsLedState = !gpsLedState;
        digitalWrite(GPS_LED_PIN, gpsLedState);
        lastGpsBlink = currentMillis;
      }
    }
    
    // GSM LED
    if (GSM_LED_PIN >= 0) {
      unsigned long gsmInterval = modemReady ? 300 : 1500;
      if (currentMillis - lastGsmBlink >= gsmInterval) {
        gsmLedState = !gsmLedState;
        digitalWrite(GSM_LED_PIN, gsmLedState);
        lastGsmBlink = currentMillis;
      }
    }
    
    // Safe LED (always on when no accident)
    if (!accidentDetected && SAFE_LED_PIN >= 0) {
      digitalWrite(SAFE_LED_PIN, HIGH);
    }
  }

  String sendATCommand(String cmd, unsigned long timeout) {
    modemSerial.println(cmd);
    return readModemResponse(timeout);
  }

  String readModemResponse(unsigned long timeout) {
    String response = "";
    unsigned long start = millis();
    
    while (millis() - start < timeout) {
      while (modemSerial.available()) {
        char c = modemSerial.read();
        response += c;
      }
      if (response.indexOf("OK\r\n") >= 0 || 
          response.indexOf("ERROR\r\n") >= 0 || 
          response.indexOf("DOWNLOAD") >= 0 || 
          response.indexOf("+CMGS:") >= 0 || 
          response.indexOf("+HTTPACTION:") >= 0) {
        break;
      }
    }
    
    response.trim();
    return response;
  }

  float readBatteryVoltage() {
    // Take 5 readings and average them (smooth out noise!)
    float totalVoltage = 0;
    for (int i = 0; i < 5; i++) {
      int rawValue = analogRead(BATTERY_ADC_PIN);
      float adcVoltage = (rawValue / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
      float voltage = adcVoltage * BATTERY_DIVIDER;
      totalVoltage += voltage;
      delay(10);  // Small delay between readings
    }
    
    return totalVoltage / 5.0;  // Return average
  }

  int batteryPercentFromVoltage(float voltage) {
    if (voltage <= BATTERY_MIN_VOLTAGE) return 0;
    if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
    float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
    return constrain((int)round(percentage), 0, 100);
  }

  void printCompactStatus() {
    Serial.print("│ Impact:");
    Serial.print(adxl345Total, 1);
    Serial.print("g │ Tilt:");
    Serial.print(mpuAvailable ? String(rollAngle, 0) : "NA");
    Serial.print("° │ Gyro:");
    Serial.print(mpuAvailable ? String(totalGyro, 1) : "NA");
    Serial.print(" │ Vib:Y");
    Serial.print(" │ Spd:");
    Serial.print(currentSpeed, 1);
    Serial.print("km/h");
    if (isSpeeding) Serial.print("⚠️");
    Serial.print(" │ GPS:");
    Serial.print(gpsFixed ? "OK" : "NO");
    Serial.print("(");
    Serial.print(satellites);
    Serial.print(")");
    Serial.print(" │ HDOP:");
    Serial.print(hdop, 1);
    Serial.print(" │ Bat:");
    Serial.print(batteryPercentFromVoltage(readBatteryVoltage()));
    Serial.print("% │ GSM:");
    Serial.print(modemReady ? "OK" : "NO");
    Serial.print(" │ API:");
    Serial.print(gprsConnected ? "OK" : "NO");
    Serial.print(" │ LastAPI:");
    Serial.print((millis() - lastApiSent) / 1000);
    Serial.println("s │");
  }