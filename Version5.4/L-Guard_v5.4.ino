/*
 * L-GUARD VEHICLE SAFETY SYSTEM v5.4 - IMPROVED
 * Changes:
 * - Fixed SMS sending with proper AT commands
 * - Added Google Maps link in SMS
 * - Cleaned up serial output (single line status)
 * - Improved modem response handlingg
 */

 #include <Wire.h>
 #include <Adafruit_Sensor.h>
 #include <Adafruit_ADXL345_U.h>
 #include <Adafruit_MPU6050.h>
 #include <TinyGPSPlus.h>
 
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
 #define VIBRATION_DIGITAL_PIN 32
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
 String DEVICE_ID = "VEHICLE_001";
 String EMERGENCY_CONTACT = "+250792957781";
 const float SPEED_LIMIT = 60.0;
 const int MIN_SATELLITES_FOR_SPEED = 5;
 const float MIN_SPEED_THRESHOLD = 5.0;
 const float MIN_GPS_HDOP = 2.5;
 const float ADXL345_LOW_THRESHOLD = 3.0;
 const float ADXL345_MEDIUM_THRESHOLD = 8.0;
 const float ADXL345_HIGH_THRESHOLD = 15.0;
 const float MPU6050_TILT_THRESHOLD = 45.0;
 const float MPU6050_ROLLOVER_THRESHOLD = 70.0;
 const float GYRO_ROTATION_THRESHOLD = 3.0;
 const float SPEED_DROP_THRESHOLD = 30.0;
 const int ROLLOVER_STABILITY_COUNT = 3;
 const unsigned long GPS_UPDATE_INTERVAL = 1000;
 const unsigned long SENSOR_READ_INTERVAL = 50;
 const unsigned long API_SYNC_INTERVAL = 30000;
 const unsigned long API_FORCE_SYNC_INTERVAL = 300000;
 const unsigned long STATUS_PRINT_INTERVAL = 2000;
 const unsigned long CANCEL_WINDOW = 10000;
 const unsigned long SPEED_BEEP_INTERVAL = 1000;
 const unsigned long GPS_WARMUP_TIME = 10000;
 const unsigned long SLEEP_IDLE_TIME = 120000;
 const float SPEED_CHANGE_THRESHOLD = 10.0;
 const float LOCATION_CHANGE_THRESHOLD = 0.0002;
 const float ACCEL_CHANGE_THRESHOLD = 2.0;
 const float ANGLE_CHANGE_THRESHOLD = 30.0;
 const float BATTERY_DIVIDER = 2.0;
 const float ADC_MAX_VALUE = 4095.0;
 const float ADC_REF_VOLTAGE = 3.3;
 const float BATTERY_MIN_VOLTAGE = 3.3;
 const float BATTERY_MAX_VOLTAGE = 4.2;
 
 // OBJECTS
 Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
 Adafruit_MPU6050 mpu;
 TinyGPSPlus gps;
 HardwareSerial modemSerial(1);
 HardwareSerial gpsSerial(2);
 
 // GLOBAL VARIABLES
 float currentLat = 0, currentLon = 0, altitude = 0;
 float currentSpeed = 0, previousSpeed = 0, heading = 0;
 bool gpsFixed = false;
 int satellites = 0;
 float hdop = 99.9;
 String utcTime = "";
 float adxl345X = 0, adxl345Y = 0, adxl345Z = 0, adxl345Total = 0;
 float adxl345MaxImpact = 0;
 float mpuAccelX = 0, mpuAccelY = 0, mpuAccelZ = 0;
 float gyroX = 0, gyroY = 0, gyroZ = 0, totalGyro = 0;
 float pitchAngle = 0, rollAngle = 0;
 bool vibrationDetected = false;
 int rolloverCount = 0;
 bool accidentDetected = false;
 bool smsSent = false;
 bool cancelPressed = false;
 unsigned long accidentTime = 0;
 String accidentType = "";
 String impactLevel = "";
 bool isSpeeding = false;
 unsigned long lastSpeedBeep = 0;
 bool speedAlertEnabled = false;
 unsigned long systemStartTime = 0;
 bool modemReady = false;
 bool gprsConnected = false;
 bool mpuAvailable = false;
 unsigned long lastSensorRead = 0;
 unsigned long lastGpsUpdate = 0;
 unsigned long lastApiSync = 0;
 unsigned long lastApiSent = 0;
 unsigned long lastStatusPrint = 0;
 unsigned long lastSignificantChange = 0;
 bool canEnterSleep = false;
 float prevSentLat = 0, prevSentLon = 0;
 float prevSentSpeed = 0;
 float prevSentAccelTotal = 0;
 float prevSentRollAngle = 0;
 int prevSentBatteryPercent = 0;
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
 void readVibration();
 void readGPS();
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
   pinMode(VIBRATION_DIGITAL_PIN, INPUT);
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
   
   // Clear any pending data
   while (modemSerial.available()) {
     modemSerial.read();
   }
   
   Serial.println("  . Powering on...");
   digitalWrite(MODEM_PWR_PIN, LOW);
   delay(100);
   digitalWrite(MODEM_PWR_PIN, HIGH);
   delay(1500);
   digitalWrite(MODEM_PWR_PIN, LOW);
   delay(5000);  // Increased delay for modem to fully boot
   
   // Initial AT test
   sendATCommand("AT", 500);
   sendATCommand("AT", 500);
   sendATCommand("ATE0", 500);  // Disable echo
   
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
   
   // Configure SMS
   // Configure SMS properly
  sendATCommand("AT+CMGF=1", 1000);  // Text mode
  sendATCommand("AT+CSCS=\"GSM\"", 1000);  // Character set
  sendATCommand("AT+CSMP=17,167,0,0", 1000);  // SMS parameters
  sendATCommand("AT+CNMI=2,1,0,0,0", 1000);  // SMS notification
  sendATCommand("AT+CSCA?", 2000);  // Check SMS center number (important!)
   
   Serial.println("OK Modem initialized!\n");
 }
 
 void checkSMSCenter() {
   Serial.println("Checking SMS Center...");
   String response = sendATCommand("AT+CSCA?", 2000);
   Serial.println("SMS Center: " + response);
   
   // If empty or error, set it manually for MTN Rwanda
   if (response.indexOf("+CSCA:") < 0) {
     Serial.println("Setting SMS Center for MTN Rwanda...");
     sendATCommand("AT+CSCA=\"+250788110000\"", 2000);  // MTN Rwanda SMS center
   }
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
   Serial.println("      L-GUARD v5.4 - IMPROVED & CLEANED");
   Serial.println("=========================================================\n");
   systemStartTime = millis();
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
     checkSMSCenter();
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
   Serial.println(" API SYNC: SMART MODE");
   Serial.println(" SMS: ENABLED");
   Serial.println("=========================================================\n");
   Serial.println("⏳ Waiting for GPS to stabilize (10 seconds)...\n");
 }
 
 void loop() {
   unsigned long currentMillis = millis();
   
   if (!isSpeeding && !accidentDetected) {
     digitalWrite(BUZZER_PIN, LOW);
   }
   
   if (!speedAlertEnabled && (currentMillis - systemStartTime >= GPS_WARMUP_TIME)) {
     speedAlertEnabled = true;
     Serial.println("✅ GPS warmed up - Speed alerts ENABLED\n");
     lastSignificantChange = currentMillis;
   }
   
   if (currentMillis - lastSensorRead >= SENSOR_READ_INTERVAL) {
     readADXL345();
     readMPU6050();
     readVibration();
     checkAccident();
     lastSensorRead = currentMillis;
   }
   
   if (currentMillis - lastGpsUpdate >= GPS_UPDATE_INTERVAL) {
     readGPS();
     checkSpeedLimit();
     lastGpsUpdate = currentMillis;
   }
   
   if (gprsConnected && currentMillis - lastApiSync >= API_SYNC_INTERVAL) {
     bool forceSync = (currentMillis - lastApiSent >= API_FORCE_SYNC_INTERVAL);
     bool hasChanges = hasSignificantChange();
     if (forceSync || hasChanges) {
       sendTrackingDataToAPI();
       lastApiSent = currentMillis;
       lastSignificantChange = currentMillis;
     }
     lastApiSync = currentMillis;
   }
   
   if (accidentDetected && !smsSent) {
     handleCancelButton();
     lastSignificantChange = currentMillis;
   }
   
   updateStatusLEDs();
   
   if (currentMillis - lastStatusPrint >= STATUS_PRINT_INTERVAL) {
     if (!accidentDetected) {
       printCompactStatus();
     }
     lastStatusPrint = currentMillis;
   }
   
   if (!accidentDetected && !isSpeeding && (currentMillis - lastSignificantChange > SLEEP_IDLE_TIME)) {
     if (!canEnterSleep) {
       Serial.println("\n💤 Entering SLEEP MODE");
       canEnterSleep = true;
     }
     esp_sleep_enable_timer_wakeup(10000000);
     esp_light_sleep_start();
     lastSignificantChange = currentMillis;
     canEnterSleep = false;
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
   pitchAngle = atan2(mpuAccelY, sqrt(mpuAccelX * mpuAccelX + mpuAccelZ * mpuAccelZ)) * 180.0 / PI;
   rollAngle = atan2(-mpuAccelX, mpuAccelZ) * 180.0 / PI;
   if (abs(rollAngle) > MPU6050_ROLLOVER_THRESHOLD || abs(pitchAngle) > MPU6050_ROLLOVER_THRESHOLD) {
     rolloverCount = constrain(rolloverCount + 1, 0, ROLLOVER_STABILITY_COUNT);
   } else {
     rolloverCount = 0;
   }
 }
 
 void readVibration() {
   vibrationDetected = (digitalRead(VIBRATION_DIGITAL_PIN) == LOW);
 }
 
 void readGPS() {
   while (gpsSerial.available() > 0) {
     char c = gpsSerial.read();
     gps.encode(c);
   }
   if (gps.location.isUpdated()) {
     currentLat = gps.location.lat();
     currentLon = gps.location.lng();
     gpsFixed = gps.location.isValid();
   }
   if (gps.altitude.isUpdated()) {
     altitude = gps.altitude.meters();
   }
   if (gps.speed.isUpdated()) {
     previousSpeed = currentSpeed;
     currentSpeed = gps.speed.kmph();
   }
   if (gps.course.isUpdated()) {
     heading = gps.course.deg();
   }
   if (gps.satellites.isUpdated()) {
     satellites = gps.satellites.value();
   }
   if (gps.hdop.isUpdated()) {
     hdop = gps.hdop.hdop();
   }
   if (gps.time.isValid()) {
     char timeStr[16];
     sprintf(timeStr, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
     utcTime = String(timeStr);
   }
 }
 
 void checkSpeedLimit() {
   unsigned long currentMillis = millis();
   if (!speedAlertEnabled) {
     digitalWrite(BUZZER_PIN, LOW);
     isSpeeding = false;
     return;
   }
   bool gpsReliable = (gpsFixed && satellites >= MIN_SATELLITES_FOR_SPEED && hdop < MIN_GPS_HDOP && currentSpeed > MIN_SPEED_THRESHOLD);
   bool allConditionsMet = (gpsReliable && currentSpeed > SPEED_LIMIT);
   if (allConditionsMet) {
     if (!isSpeeding) {
       isSpeeding = true;
       Serial.println("\n⚠️ OVERSPEEDING: " + String(currentSpeed, 1) + " km/h");
     }
     if (currentMillis - lastSpeedBeep >= SPEED_BEEP_INTERVAL) {
       digitalWrite(BUZZER_PIN, HIGH);
       delay(200);
       digitalWrite(BUZZER_PIN, LOW);
       lastSpeedBeep = currentMillis;
     }
   } else {
     digitalWrite(BUZZER_PIN, LOW);
     if (isSpeeding) {
       Serial.println("✅ Speed normal");
     }
     isSpeeding = false;
   }
 }
 
 void checkAccident() {
   if (accidentDetected) return;
   bool vibrationGate = vibrationDetected;
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
   }
 }
 
 void handleCancelButton() {
   unsigned long elapsed = millis() - accidentTime;
   if (digitalRead(OK_BUTTON_PIN) == LOW) {
     delay(50);
     if (digitalRead(OK_BUTTON_PIN) == LOW) {
       cancelPressed = true;
       Serial.println("\n✅ ALERT CANCELLED BY USER!\n");
       accidentDetected = false;
       smsSent = false;
       adxl345MaxImpact = 0;
       rolloverCount = 0;
       digitalWrite(BUZZER_PIN, LOW);
       if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW);
       if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
       while (digitalRead(OK_BUTTON_PIN) == LOW) delay(10);
       return;
     }
   }
   if (elapsed >= CANCEL_WINDOW && !cancelPressed && !smsSent) {
     Serial.println("\n⏰ TIME EXPIRED - Sending alerts!\n");
     sendEmergencySMS();
     if (gprsConnected) {
       sendAccidentToAPI();
     }
   }
 }
 
 bool hasSignificantChange() {
   bool anyChange = false;
   if (gpsFixed) {
     float latDiff = abs(currentLat - prevSentLat);
     float lonDiff = abs(currentLon - prevSentLon);
     if (latDiff > LOCATION_CHANGE_THRESHOLD || lonDiff > LOCATION_CHANGE_THRESHOLD) {
       anyChange = true;
     }
   }
   float speedDiff = abs(currentSpeed - prevSentSpeed);
   if (speedDiff > SPEED_CHANGE_THRESHOLD) anyChange = true;
   float accelDiff = abs(adxl345Total - prevSentAccelTotal);
   if (accelDiff > ACCEL_CHANGE_THRESHOLD) anyChange = true;
   if (mpuAvailable) {
     float angleDiff = abs(rollAngle - prevSentRollAngle);
     if (angleDiff > ANGLE_CHANGE_THRESHOLD) anyChange = true;
   }
   int currentBattPercent = batteryPercentFromVoltage(readBatteryVoltage());
   if (abs(currentBattPercent - prevSentBatteryPercent) >= 10) anyChange = true;
   bool startedMoving = (prevSentSpeed < MIN_SPEED_THRESHOLD && currentSpeed > MIN_SPEED_THRESHOLD);
   if (startedMoving) anyChange = true;
   bool justStopped = (prevSentSpeed > MIN_SPEED_THRESHOLD && currentSpeed < MIN_SPEED_THRESHOLD);
   if (justStopped) anyChange = true;
   if (anyChange) {
     lastSignificantChange = millis();
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
   json += "\"speed\":" + String((int)currentSpeed) + ",";
   json += "\"heading\":" + String((int)heading) + ",";
   json += "\"accelerationX\":" + String(adxl345X, 1) + ",";
   json += "\"accelerationY\":" + String(adxl345Y, 1) + ",";
   json += "\"accelerationZ\":" + String(adxl345Z, 1) + ",";
   json += "\"gyroX\":" + String((int)gyroX) + ",";
   json += "\"gyroY\":" + String((int)gyroY) + ",";
   json += "\"gyroZ\":" + String((int)gyroZ) + ",";
   json += "\"rpm\":0,";
   json += "\"engineTemp\":0,";
   json += "\"fuelLevel\":0,";
   json += "\"batteryLevel\":" + String(batteryPercentFromVoltage(readBatteryVoltage())) + ",";
   json += "\"signalStrength\":85,";
   json += "\"rawData\":{}";
   json += "}";
   return json;
 }
 
 bool httpPOST_Fixed(String jsonData) {
   if (!gprsConnected) {
     Serial.println("X GPRS not connected");
     return false;
   }
   Serial.println("\n→ Sending to API...");
   sendATCommand("AT+HTTPTERM", 1000);
   delay(1000);
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
       modemSerial.println("AT+HTTPACTION=1");
       for (int i = 0; i < 35; i++) {
         delay(1000);
         if (modemSerial.available()) {
           String result = readModemResponse(2000);
           if (result.indexOf("+HTTPACTION") >= 0) {
             if (result.indexOf(",200,") >= 0 || result.indexOf(",201,") >= 0) {
               Serial.println("   ✅ API SUCCESS!");
               success = true;
             } else {
               Serial.println("   ❌ API Failed: " + result);
             }
             break;
           }
         }
       }
     }
   }
   sendATCommand("AT+HTTPTERM", 1000);
   return success;
 }
 
 void sendTrackingDataToAPI() {
   Serial.println("\n📤 Sending telemetry...");
   String jsonData = buildTelemetryJSON();
   if (httpPOST_Fixed(jsonData)) {
     prevSentLat = currentLat;
     prevSentLon = currentLon;
     prevSentSpeed = currentSpeed;
     prevSentAccelTotal = adxl345Total;
     prevSentRollAngle = rollAngle;
     prevSentBatteryPercent = batteryPercentFromVoltage(readBatteryVoltage());
     lastApiSent = millis();
     lastSignificantChange = millis();
   }
 }
 
 void sendAccidentToAPI() {
   Serial.println("\n🚨 Sending accident data...");
   String jsonData = buildTelemetryJSON();
   if (httpPOST_Fixed(jsonData)) {
     Serial.println("✅ Accident logged!\n");
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
   
   // Ensure text mode
   sendATCommand("AT+CMGF=1", 1000);
   delay(500);
   
   // Build message with Google Maps link
   String message = "!! VEHICLE ACCIDENT !!\n";
   message += "Device: " + DEVICE_ID + "\n";
   message += "Type: " + accidentType + "\n";
   message += "Impact: " + String(adxl345MaxImpact, 2) + "g\n";
   message += "Time: " + utcTime + "\n";
   
   if (gpsFixed && currentLat != 0 && currentLon != 0) {
     message += "Speed: " + String(currentSpeed, 1) + " km/h\n\n";
     message += "LOCATION:\n";
     message += String(currentLat, 6) + "," + String(currentLon, 6) + "\n\n";
     message += "Google Maps:\n";
     message += "https://maps.google.com/maps?q=" + String(currentLat, 6) + "," + String(currentLon, 6);
   } else {
     message += "\nGPS: No fix\nCall immediately!";
   }
   
   Serial.println("Message: " + message);
   Serial.println("To: " + EMERGENCY_CONTACT);
   
   // Clear serial buffer
   while (modemSerial.available()) {
     modemSerial.read();
   }
   
   // Send SMS command with text mode format
   modemSerial.print("AT+CMGS=\"");
   modemSerial.print(EMERGENCY_CONTACT);
   modemSerial.print("\"");
   modemSerial.write('\r');  // Use carriage return instead of println
   delay(1000);
   
   // Wait for '>' prompt
   String prompt = "";
   unsigned long startWait = millis();
   while (millis() - startWait < 5000) {
     if (modemSerial.available()) {
       char c = modemSerial.read();
       prompt += c;
       Serial.print(c);
       if (c == '>') {
         break;
       }
     }
   }
   
   if (prompt.indexOf('>') >= 0) {
     Serial.println("\n✓ Got prompt, sending message...");
     
     // Send message character by character (more reliable)
     for (int i = 0; i < message.length(); i++) {
     modemSerial.write(message[i]);
     delay(5);  // Small delay between characters
     }  
    delay(500);
 
     // Send Ctrl+Z
     modemSerial.write(26);
     modemSerial.write('\r');  // Add carriage return
     Serial.println("✓ Sent Ctrl+Z");
     
     // Wait for response
     String response = "";
     startWait = millis();
     while (millis() - startWait < 15000) {
       if (modemSerial.available()) {
         char c = modemSerial.read();
         response += c;
         Serial.print(c);
       }
       if (response.indexOf("+CMGS:") >= 0 || response.indexOf("OK") >= 0) {
         Serial.println("\n✅ SMS SENT SUCCESSFULLY!");
         smsSent = true;
         return;
       }
       if (response.indexOf("ERROR") >= 0) {
         Serial.println("\n❌ SMS ERROR: " + response);
         return;
       }
     }
     
     if (!smsSent) {
       Serial.println("\n❌ SMS Timeout - no confirmation");
     }
   } else {
     Serial.println("\n❌ No '>' prompt received");
     Serial.println("Response: " + prompt);
   }
 }
 
 void updateStatusLEDs() {
   unsigned long currentMillis = millis();
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
     return;
   } else {
     if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW);
   }
   if (GPS_LED_PIN >= 0) {
     unsigned long gpsInterval = gpsFixed ? 250 : 1000;
     if (currentMillis - lastGpsBlink >= gpsInterval) {
       gpsLedState = !gpsLedState;
       digitalWrite(GPS_LED_PIN, gpsLedState);
       lastGpsBlink = currentMillis;
     }
   }
   if (GSM_LED_PIN >= 0) {
     unsigned long gsmInterval = modemReady ? 300 : 1500;
     if (currentMillis - lastGsmBlink >= gsmInterval) {
       gsmLedState = !gsmLedState;
       digitalWrite(GSM_LED_PIN, gsmLedState);
       lastGsmBlink = currentMillis;
     }
   }
   if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
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
     if (response.indexOf("OK\r\n") >= 0 || response.indexOf("ERROR\r\n") >= 0 || response.indexOf("DOWNLOAD") >= 0 || response.indexOf("+CMGS:") >= 0 || response.indexOf("+HTTPACTION:") >= 0) {
       break;
     }
   }
   response.trim();
   return response;
 }
 
 float readBatteryVoltage() {
   int rawValue = analogRead(BATTERY_ADC_PIN);
   float adcVoltage = (rawValue / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
   float batteryVoltage = adcVoltage * BATTERY_DIVIDER;
   return batteryVoltage;
 }
 
 int batteryPercentFromVoltage(float voltage) {
   if (voltage <= BATTERY_MIN_VOLTAGE) return 0;
   if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
   float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
   return constrain((int)round(percentage), 0, 100);
 }
 
 void printCompactStatus() {
   // Single line status output
   Serial.print("│ Impact:");
   Serial.print(adxl345Total, 1);
   Serial.print("g │ Tilt:");
   Serial.print(mpuAvailable ? String(rollAngle, 0) : "NA");
   Serial.print("° │ Gyro:");
   Serial.print(mpuAvailable ? String(totalGyro, 1) : "NA");
   Serial.print(" │ Vib:");
   Serial.print(vibrationDetected ? "Y" : "N");
   Serial.print(" │ Spd:");
   Serial.print(currentSpeed, 0);
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
   Serial.println(" │");
 }
 