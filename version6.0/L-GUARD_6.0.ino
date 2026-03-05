/*
 * L-GUARD VEHICLE SAFETY SYSTEM v6.0 - ADXL375 UPGRADE
 * 
 * CHANGES FROM v5.5:
 * ✅ Migrated from ADXL345 (±16g) to ADXL375 (±200g) 
 * ✅ Better high-impact detection for serious accidents
 * ✅ Adjusted thresholds for 200g range
 * ✅ All other features maintained (GPS, API, SMS, MPU6050)
 * ✅ Fixed I2C address (0x53) for ADXL375
 */

 #include <Wire.h>
 #include <Adafruit_Sensor.h>
 #include <Adafruit_ADXL375.h>  // ← CHANGED: New library!
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
 #define ADXL375_ADDRESS 0x53  // ← NEW: Fixed I2C address
 
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
 
 // ========== ADXL375 THRESHOLDS (±200g RANGE!) ==========
 // ADXL375 is WAY more sensitive - adjusted thresholds!
 const float ADXL375_LOW_THRESHOLD = 8.0;        // ← CHANGED: 8g (was 3g)
 const float ADXL375_MEDIUM_THRESHOLD = 25.0;    // ← CHANGED: 25g (was 8g)  
 const float ADXL375_HIGH_THRESHOLD = 50.0;      // ← CHANGED: 50g (was 15g)
 const float ADXL375_EXTREME_THRESHOLD = 100.0;  // ← NEW: Extreme impacts!
 
 const float MPU6050_TILT_THRESHOLD = 45.0;
 const float MPU6050_ROLLOVER_THRESHOLD = 70.0;
 const float GYRO_ROTATION_THRESHOLD = 3.0;
 const float SPEED_DROP_THRESHOLD = 30.0;
 const int ROLLOVER_STABILITY_COUNT = 3;
 const unsigned long SENSOR_READ_INTERVAL = 50;
 const unsigned long GPS_UPDATE_INTERVAL = 1000;
 const unsigned long API_FORCE_SYNC_INTERVAL = 900000;
 const unsigned long STATUS_PRINT_INTERVAL = 2000;
 const unsigned long CANCEL_WINDOW = 10000;
 const unsigned long SPEED_BEEP_INTERVAL = 1000;
 const unsigned long GPS_WARMUP_TIME = 10000;
 const unsigned long SLEEP_IDLE_TIME = 120000;
 const unsigned long ACCIDENT_COOLDOWN = 30000;
 const unsigned long EMERGENCY_INTERRUPT_TIMEOUT = 2000;
 const float SPEED_CHANGE_THRESHOLD = 10.0;
 const float LOCATION_CHANGE_THRESHOLD = 0.0002;
 const float ACCEL_CHANGE_THRESHOLD = 5.0;  // ← CHANGED: Higher for ADXL375
 const float ANGLE_CHANGE_THRESHOLD = 30.0;
 const float BATTERY_DIVIDER = 2.0;
 const float ADC_MAX_VALUE = 4095.0;
 const float ADC_REF_VOLTAGE = 3.3;
 const float BATTERY_MIN_VOLTAGE = 3.3;
 const float BATTERY_MAX_VOLTAGE = 4.2;
 
 // MPU6050 CALIBRATION
 float rollAngleOffset = 0;
 float pitchAngleOffset = 0;
 bool mpuCalibrated = false;
 
 // OBJECTS
 Adafruit_ADXL375 accel = Adafruit_ADXL375(12345);
 Adafruit_MPU6050 mpu;
 HardwareSerial modemSerial(1);
 HardwareSerial gpsSerial(2);
 
 // GPS VARIABLES
 String gpsBuffer = "";
 bool gpsFixed = false;
 float currentLat = 0, currentLon = 0, altitude = 0;
 float currentSpeed = 0, previousSpeed = 0, heading = 0;
 int satellites = 0;
 float hdop = 99.9;
 String utcTime = "";
 String fixQuality = "No Fix";
 
 // SENSOR VARIABLES (ADXL375 now!)
 float adxl375X = 0, adxl375Y = 0, adxl375Z = 0, adxl375Total = 0;  // ← RENAMED!
 float adxl375MaxImpact = 0;  // ← RENAMED!
 float mpuAccelX = 0, mpuAccelY = 0, mpuAccelZ = 0;
 float gyroX = 0, gyroY = 0, gyroZ = 0, totalGyro = 0;
 float pitchAngle = 0, rollAngle = 0;
 int rolloverCount = 0;
 
 // STATE VARIABLES
 bool accidentDetected = false;
 bool smsSent = false;
 bool cancelPressed = false;
 unsigned long accidentTime = 0;
 unsigned long lastAccidentHandled = 0;
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
 unsigned long lastApiSent = 0;
 unsigned long lastStatusPrint = 0;
 unsigned long lastSignificantChange = 0;
 unsigned long lastMovementTime = 0;
 
 // API SYNC CONTROL
 bool apiSyncInProgress = false;
 bool forceNextSync = false;
 
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
 
 // FUNCTION PROTOTYPES (same as before, just use adxl375 names)
 void setupPins();
 void setupSensors();
 void setupGPS();
 void setupModem();
 bool connectGPRS();
 void readADXL375();  // ← RENAMED!
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
 bool httpPOST_Accident(String jsonData);
 float readBatteryVoltage();
 int batteryPercentFromVoltage(float v);
 String buildTelemetryJSON();
 String buildAccidentJSON();
 void printCompactStatus();
 bool isSystemReallyStable();
 bool sendSingleSMS(String message);
 void displayDataRate(void);  // ← NEW: Debug function
 
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
 
 // ========== ADXL375 SETUP (UPDATED!) ==========
 void setupSensors() {
   Serial.println("Initializing sensors...");
   Wire.begin(21, 22);  // ESP32 I2C pins
   
   Serial.print("  . ADXL375 (±200g)... ");
   
   // Try to initialize ADXL375 at address 0x53
   if (!accel.begin(ADXL375_ADDRESS)) {
     Serial.println("X FAILED!");
     Serial.println("    Check wiring:");
     Serial.println("    - VCC → 3.3V");
     Serial.println("    - GND → GND");
     Serial.println("    - SDA → GPIO21");
     Serial.println("    - SCL → GPIO22");
     Serial.println("    - ALT ADDRESS pin → GND (for 0x53)");
     
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
   
   // ========== ADXL375 CONFIGURATION ==========
   // Set data rate (100Hz is good balance between speed and power)
   accel.setDataRate(ADXL343_DATARATE_100_HZ);
   
   // Note: ADXL375 range is FIXED at ±200g (no setRange needed!)
   
   // Print sensor info
   Serial.println("\n    ADXL375 Details:");
   accel.printSensorDetails();
   displayDataRate();
   Serial.println("    Range: ±200g (FIXED)\n");
   
   // MPU6050 setup (same as before)
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
     
     // Calibrate MPU6050
     Serial.print("  . Calibrating MPU6050... ");
     delay(500);
     
     sensors_event_t a, g, temp;
     float totalRoll = 0, totalPitch = 0;
     
     for (int i = 0; i < 10; i++) {
       mpu.getEvent(&a, &g, &temp);
       float pitch = atan2(a.acceleration.y, sqrt(a.acceleration.x * a.acceleration.x + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
       float roll = atan2(-a.acceleration.x, a.acceleration.z) * 180.0 / PI;
       totalRoll += roll;
       totalPitch += pitch;
       delay(50);
     }
     
     rollAngleOffset = totalRoll / 10.0;
     pitchAngleOffset = totalPitch / 10.0;
     mpuCalibrated = true;
     
     Serial.println("OK");
     Serial.println("    Roll offset: " + String(rollAngleOffset, 1) + "°");
     Serial.println("    Pitch offset: " + String(pitchAngleOffset, 1) + "°");
     Serial.println("    ✅ Current position set as ZERO!");
   }
   
   Serial.println("\nOK Sensors initialized!\n");
 }
 
 // ========== DISPLAY ADXL375 DATA RATE ==========
 void displayDataRate(void) {
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
     case ADXL343_DATARATE_3_13_HZ: Serial.print("3.13"); break;
     case ADXL343_DATARATE_1_56_HZ: Serial.print("1.56"); break;
     case ADXL343_DATARATE_0_78_HZ: Serial.print("0.78"); break;
     case ADXL343_DATARATE_0_39_HZ: Serial.print("0.39"); break;
     case ADXL343_DATARATE_0_20_HZ: Serial.print("0.20"); break;
     case ADXL343_DATARATE_0_10_HZ: Serial.print("0.10"); break;
     default:                       Serial.print("????"); break;
   }
   Serial.println(" Hz");
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
   Serial.println("      L-GUARD v6.0 - ADXL375 UPGRADE");
   Serial.println("=========================================================\n");
   
   systemStartTime = millis();
   lastMovementTime = millis();
   
   setupPins();
   
   // LED test
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
   Serial.println(" ACCIDENT DETECTION: ACTIVE (±200g RANGE!)");
   Serial.println(" GPS TRACKING: ACTIVE");
   Serial.println(" SPEED ALERT: > 40 km/h");
   Serial.println(" API SYNC: SMART MODE");
   Serial.println(" SMS: ENABLED");
   Serial.println("=========================================================\n");
   Serial.println("⏳ Waiting for GPS to stabilize (10 seconds)...\n");
   
   // Initialize previous values
   prevSentLat = currentLat;
   prevSentLon = currentLon;
   prevSentSpeed = currentSpeed;
   prevSentAccelTotal = adxl375Total;
   prevSentRollAngle = rollAngle;
   prevSentBatteryPercent = batteryPercentFromVoltage(readBatteryVoltage());
   lastApiSent = millis();
 }
 
 // ========== READ ADXL375 (UPDATED!) ==========
 void readADXL375() {
   sensors_event_t event;
   accel.getEvent(&event);
   
   // ADXL375 returns m/s² - convert to g
   const float MPS2_TO_G = 1.0 / 9.80665;
   
   adxl375X = event.acceleration.x * MPS2_TO_G;
   adxl375Y = event.acceleration.y * MPS2_TO_G;
   adxl375Z = event.acceleration.z * MPS2_TO_G;
   
   // Calculate total magnitude
   adxl375Total = sqrt(adxl375X * adxl375X + adxl375Y * adxl375Y + adxl375Z * adxl375Z);
   
   // Track max impact
   if (adxl375Total > adxl375MaxImpact) {
     adxl375MaxImpact = adxl375Total;
   }
   
   // Track movement for sleep mode
   static float lastAccelForMovement = 1.0;
   
   bool speedChanging = (abs(currentSpeed - previousSpeed) > 2.0);
   bool accelChanging = (abs(adxl375Total - lastAccelForMovement) > 0.5);  // ← Higher threshold for ADXL375
   
   if (speedChanging || accelChanging || currentSpeed > 5.0) {
     lastMovementTime = millis();
   }
   
   lastAccelForMovement = adxl375Total;
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
   
   float rawPitch = atan2(mpuAccelY, sqrt(mpuAccelX * mpuAccelX + mpuAccelZ * mpuAccelZ)) * 180.0 / PI;
   float rawRoll = atan2(-mpuAccelX, mpuAccelZ) * 180.0 / PI;
   
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
 
 // ========== GPS FUNCTIONS (SAME AS BEFORE) ==========
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
   
   if (parts[1].length() >= 6) {
     String t = parts[1];
     utcTime = t.substring(0,2) + ":" + t.substring(2,4) + ":" + t.substring(4,6);
   }
   
   int fixNum = parts[6].toInt();
   switch(fixNum) {
     case 0: fixQuality = "No Fix"; gpsFixed = false; break;
     case 1: fixQuality = "GPS Fix"; gpsFixed = true; break;
     case 2: fixQuality = "DGPS Fix"; gpsFixed = true; break;
     default: fixQuality = "Unknown"; gpsFixed = false;
   }
   
   satellites = parts[7].toInt();
   
   if (parts[8].length() > 0) {
     hdop = parts[8].toFloat();
   }
   
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
   
   bool validData = (parts[2] == "A");
   if (!validData) return;
   
   if (parts[7].length() > 0) {
     float speedKnots = parts[7].toFloat();
     previousSpeed = currentSpeed;
     currentSpeed = speedKnots * 1.852;
     
     if (currentSpeed < 0 || currentSpeed > 300) {
       currentSpeed = previousSpeed;
     }
     
     if (!gpsFixed || satellites < MIN_SATELLITES_FOR_SPEED || hdop > MIN_GPS_HDOP) {
       if (abs(currentSpeed - previousSpeed) > 20) {
         currentSpeed = previousSpeed;
       }
     }
   }
   
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
 
 // ========== ACCIDENT DETECTION (UPDATED THRESHOLDS!) ==========
 void checkAccident() {
   if (millis() - lastAccidentHandled < ACCIDENT_COOLDOWN) {
     return;
   }
   
   if (accidentDetected) return;
   
   static bool emergencyInterruptNeeded = false;
   if (emergencyInterruptNeeded && apiSyncInProgress) {
     Serial.println("\n⚠️  EMERGENCY: Stopping API sync for accident!");
     sendATCommand("AT+HTTPTERM", 500);
     apiSyncInProgress = false;
     emergencyInterruptNeeded = false;
   }
   
   bool vibrationGate = true;
   
   // ========== ADXL375 IMPACT LEVELS (±200g RANGE!) ==========
   bool lowImpact = (adxl375Total > ADXL375_LOW_THRESHOLD);        // > 8g
   bool mediumImpact = (adxl375Total > ADXL375_MEDIUM_THRESHOLD);  // > 25g
   bool highImpact = (adxl375Total > ADXL375_HIGH_THRESHOLD);      // > 50g
   bool extremeImpact = (adxl375Total > ADXL375_EXTREME_THRESHOLD); // > 100g
   
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
   
   // ========== ACCIDENT DETECTION LOGIC (PRIORITY ORDER!) ==========
   if (extremeImpact && vibrationGate) {
     accidentType = "CATASTROPHIC_IMPACT";  // ← NEW: Extreme accidents!
     impactLevel = "EXTREME";
     accidentCondition = true;
   } else if (highImpact && vibrationGate) {
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
     
     if (apiSyncInProgress) {
       Serial.println("⚠️  API busy - will interrupt on next check!");
       emergencyInterruptNeeded = true;
     }
     
     if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, LOW);
     
     Serial.println("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
     Serial.println("   🚨 ACCIDENT DETECTED! 🚨");
     Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
     Serial.println("Type: " + accidentType);
     Serial.println("Severity: " + impactLevel);
     Serial.println("Impact: " + String(adxl375Total, 1) + "g");  // ← Show ADXL375 reading!
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
   
   if (digitalRead(OK_BUTTON_PIN) == LOW) {
     delay(50);
     if (digitalRead(OK_BUTTON_PIN) == LOW) {
       cancelPressed = true;
       Serial.println("\n✅ ALERT CANCELLED BY USER!\n");
       
       accidentDetected = false;
       smsSent = false;
       adxl375MaxImpact = 0;
       rolloverCount = 0;
       lastAccidentHandled = millis();
       
       digitalWrite(BUZZER_PIN, LOW);
       if (DANGER_LED_PIN >= 0) digitalWrite(DANGER_LED_PIN, LOW);
       if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
       
       while (digitalRead(OK_BUTTON_PIN) == LOW) delay(10);
       return;
     }
   }
   
   if (elapsed >= CANCEL_WINDOW && !cancelPressed && !smsSent) {
     Serial.println("\n⏰ TIME EXPIRED - Sending alerts!\n");
     
     if (apiSyncInProgress) {
       Serial.println("⚠️  INTERRUPTING API SYNC FOR EMERGENCY!");
       sendATCommand("AT+HTTPTERM", 1000);
       apiSyncInProgress = false;
     }
     
     sendEmergencySMS();
     
     Serial.println("⏳ Waiting 3s for SMS to complete...");
     delay(3000);
     
     sendATCommand("AT", 500);
     sendATCommand("AT+CMGF=1", 500);
     
     if (gprsConnected) {
       sendAccidentToAPI();
     }
     
     accidentDetected = false;
     adxl375MaxImpact = 0;
     rolloverCount = 0;
     lastAccidentHandled = millis();
     forceNextSync = true;
     
     if (SAFE_LED_PIN >= 0) digitalWrite(SAFE_LED_PIN, HIGH);
     
     Serial.println("\n✅ Accident handled - monitoring resumed\n");
   }
 }
 
 bool hasSignificantChange() {
   bool anyChange = false;
   
   float latDiff = 0, lonDiff = 0;
   float speedDiff = 0;
   float accelDiff = 0;
   float angleDiff = 0;
   
   if (gpsFixed) {
     latDiff = abs(currentLat - prevSentLat);
     lonDiff = abs(currentLon - prevSentLon);
     if (latDiff > LOCATION_CHANGE_THRESHOLD || lonDiff > LOCATION_CHANGE_THRESHOLD) {
       anyChange = true;
     }
   }
   
   speedDiff = abs(currentSpeed - prevSentSpeed);
   if (speedDiff > SPEED_CHANGE_THRESHOLD) {
     anyChange = true;
   }
   
   accelDiff = abs(adxl375Total - prevSentAccelTotal);
   if (accelDiff > ACCEL_CHANGE_THRESHOLD) {
     anyChange = true;
   }
   
   if (mpuAvailable) {
     angleDiff = abs(rollAngle - prevSentRollAngle);
     if (angleDiff > ANGLE_CHANGE_THRESHOLD) {
       anyChange = true;
     }
   }
   
   int currentBattPercent = batteryPercentFromVoltage(readBatteryVoltage());
   if (abs(currentBattPercent - prevSentBatteryPercent) >= 20) {
     anyChange = true;
   }
   
   bool wasMoving = (prevSentSpeed > MIN_SPEED_THRESHOLD);
   bool isMoving = (currentSpeed > MIN_SPEED_THRESHOLD);
   
   if (wasMoving != isMoving) {
     anyChange = true;
   }
   
   bool onlyBatteryChanged = anyChange && 
                             (latDiff <= LOCATION_CHANGE_THRESHOLD) &&
                             (lonDiff <= LOCATION_CHANGE_THRESHOLD) &&
                             (speedDiff <= SPEED_CHANGE_THRESHOLD) &&
                             (accelDiff <= ACCEL_CHANGE_THRESHOLD) &&
                             (angleDiff <= ANGLE_CHANGE_THRESHOLD) &&
                             (!wasMoving && !isMoving);
   
   if (onlyBatteryChanged) {
     return false;
   }
   
   return anyChange;
 }
 
 // ========== JSON BUILDERS (UPDATED FOR ADXL375!) ==========
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
   json += "\"accelerationX\":" + String(adxl375X, 2) + ",";  // ← ADXL375!
   json += "\"accelerationY\":" + String(adxl375Y, 2) + ",";
   json += "\"accelerationZ\":" + String(adxl375Z, 2) + ",";
   json += "\"gyroX\":" + String(gyroX, 2) + ",";
   json += "\"gyroY\":" + String(gyroY, 2) + ",";
   json += "\"gyroZ\":" + String(gyroZ, 2) + ",";
   json += "\"rpm\":0,";
   json += "\"engineTemp\":0,";
   json += "\"fuelLevel\":0,";
   json += "\"batteryLevel\":" + String(batteryPercentFromVoltage(readBatteryVoltage())) + ",";
   json += "\"signalStrength\":85,";
   
   json += "\"rawData\":{";
   json += "\"totalAccel\":" + String(adxl375Total, 2) + ",";  // ← ADXL375!
   json += "\"totalGyro\":" + String(totalGyro, 2) + ",";
   json += "\"pitchAngle\":" + String(pitchAngle, 1) + ",";
   json += "\"rollAngle\":" + String(rollAngle, 1) + ",";
   json += "\"vibration\":false,";
   json += "\"satellites\":" + String(satellites) + ",";
   json += "\"hdop\":" + String(hdop, 1) + ",";
   json += "\"fixQuality\":\"" + fixQuality + "\",";
   json += "\"accidentDetected\":" + String(accidentDetected ? "true" : "false");
   if (accidentDetected || smsSent) {
     json += ",\"accidentType\":\"" + accidentType + "\",";
     json += "\"impactLevel\":\"" + impactLevel + "\",";
     json += "\"maxImpact\":" + String(adxl375MaxImpact, 1);  // ← ADXL375!
   }
   json += "}";
   
   json += "}";
   return json;
 }
 
 String buildAccidentJSON() {
   String severity;
   if (impactLevel == "EXTREME") {        // ← NEW!
     severity = "SEVERE";  // API doesn't have "EXTREME", map to SEVERE
   } else if (impactLevel == "HIGH") {
     severity = "SEVERE";
   } else if (impactLevel == "MEDIUM") {
     severity = "MODERATE";
   } else {
     severity = "MINOR";
   }
   
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
 
 
 bool httpPOST_Fixed(String jsonData) {
   if (!gprsConnected || apiSyncInProgress) {
     return false;
   }
   
   apiSyncInProgress = true;
   
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
       modemSerial.println("AT+HTTPACTION=1");
       
       for (int i = 0; i < 15; i++) {
         delay(500);
         if (modemSerial.available()) {
           String result = readModemResponse(2000);
           if (result.indexOf("+HTTPACTION") >= 0) {
             if (result.indexOf(",200,") >= 0 || result.indexOf(",201,") >= 0) {
               success = true;
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
 
 bool httpPOST_Accident(String jsonData) {
   if (!gprsConnected || apiSyncInProgress) {
     return false;
   }
   
   apiSyncInProgress = true;
   
   sendATCommand("AT+HTTPTERM", 1000);
   delay(500);
   sendATCommand("AT+CSSLCFG=\"enableSNI\",0,1", 2000);
   sendATCommand("AT+HTTPINIT", 2000);
   
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
     prevSentLat = currentLat;
     prevSentLon = currentLon;
     prevSentSpeed = currentSpeed;
     prevSentAccelTotal = adxl375Total;
     prevSentRollAngle = rollAngle;
     prevSentBatteryPercent = batteryPercentFromVoltage(readBatteryVoltage());
     lastApiSent = millis();
   }
 }
 
 void sendAccidentToAPI() {
   if (apiSyncInProgress) {
     Serial.println("⚠️  API sync in progress - queuing accident data");
     forceNextSync = true;
     return;
   }
   
   Serial.println("\n🚨 Sending accident data to /accidents/new...");
   String jsonData = buildAccidentJSON();
   
   if (httpPOST_Accident(jsonData)) {
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
   
   String message1 = "!! ACCIDENT !!\n";
   message1 += DEVICE_ID + "\n";
   message1 += accidentType + "\n";
   message1 += "Impact: " + String(adxl375MaxImpact, 1) + "g\n";  // ← ADXL375!
   message1 += "Time: " + utcTime;
   
   if (gpsFixed && currentLat != 0 && currentLon != 0) {
     message1 += "\nSpeed: " + String(currentSpeed, 0) + "km/h";
   } else {
     message1 += "\nGPS: NO FIX!";
   }
   
   Serial.println("MSG1 length: " + String(message1.length()) + " chars");
   Serial.println("To: " + EMERGENCY_CONTACT);
   
   bool msg1Success = sendSingleSMS(message1);
   
   if (msg1Success && gpsFixed && currentLat != 0 && currentLon != 0) {
     delay(2000);
     
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
   float totalVoltage = 0;
   for (int i = 0; i < 5; i++) {
     int rawValue = analogRead(BATTERY_ADC_PIN);
     float adcVoltage = (rawValue / ADC_MAX_VALUE) * ADC_REF_VOLTAGE;
     float voltage = adcVoltage * BATTERY_DIVIDER;
     totalVoltage += voltage;
     delay(10);
   }
   
   return totalVoltage / 5.0;
 }
 
 int batteryPercentFromVoltage(float voltage) {
   if (voltage <= BATTERY_MIN_VOLTAGE) return 0;
   if (voltage >= BATTERY_MAX_VOLTAGE) return 100;
   float percentage = ((voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0;
   return constrain((int)round(percentage), 0, 100);
 }
 
 void printCompactStatus() {
   Serial.print("│ Impact:");
   Serial.print(adxl375Total, 1);  // ← ADXL375!
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
 
 bool isSystemReallyStable() {
   static float lastAccelCheck = 0;
   static float lastRollCheck = 0;
   static float lastSpeedCheck = 0;
   static unsigned long lastStableCheck = 0;
   static int stableCount = 0;
   
   unsigned long currentMillis = millis();
   
   if (currentMillis - lastStableCheck < 2000) {
     return false;
   }
   
   bool accelStable = (abs(adxl375Total - lastAccelCheck) < 0.5);  // ← Higher for ADXL375
   bool rollStable = (abs(rollAngle - lastRollCheck) < 5.0);
   bool speedStable = (currentSpeed < 1.0);
   bool notMoving = (currentMillis - lastMovementTime > SLEEP_IDLE_TIME);
   
   if (accelStable && rollStable && speedStable && notMoving) {
     stableCount++;
   } else {
     stableCount = 0;
   }
   
   lastAccelCheck = adxl375Total;
   lastRollCheck = rollAngle;
   lastSpeedCheck = currentSpeed;
   lastStableCheck = currentMillis;
   
   return (stableCount >= 5);
 }
 
 void loop() {
   unsigned long currentMillis = millis();
   
   if (currentMillis - lastSensorRead >= SENSOR_READ_INTERVAL) {
     readADXL375();  // ← ADXL375!
     readMPU6050();
     checkAccident();
     lastSensorRead = currentMillis;
   }
   
   handleGPS();
   
   if (currentMillis - lastGpsUpdate >= GPS_UPDATE_INTERVAL) {
     checkSpeedLimit();
     lastGpsUpdate = currentMillis;
   }
   
   if (!speedAlertEnabled && (currentMillis - systemStartTime >= GPS_WARMUP_TIME)) {
     speedAlertEnabled = true;
     Serial.println("✅ GPS warmed up - Speed alerts ENABLED\n");
     lastSignificantChange = currentMillis;
   }
   
   if (gprsConnected && !apiSyncInProgress) {
     bool forceSync = (currentMillis - lastApiSent >= API_FORCE_SYNC_INTERVAL);
     bool hasChanges = hasSignificantChange();
     
     if (hasChanges || forceSync || forceNextSync) {
       sendTrackingDataToAPI();
       lastApiSync = currentMillis;
       forceNextSync = false;
     }
   }
   
   if (accidentDetected && !smsSent) {
     handleCancelButton();
   }
   
   updateStatusLEDs();
   
   if (currentMillis - lastStatusPrint >= STATUS_PRINT_INTERVAL) {
     if (!accidentDetected && !apiSyncInProgress) {
       printCompactStatus();
     }
     lastStatusPrint = currentMillis;
   }
   
   if (isSystemReallyStable() && !accidentDetected && !isSpeeding && !apiSyncInProgress) {
     Serial.println("\n💤 SLEEP (all sensors stable)");
     esp_sleep_enable_timer_wakeup(10000000);
     esp_light_sleep_start();
     lastMovementTime = millis();
   }
   
   if (!isSpeeding && !accidentDetected) {
     digitalWrite(BUZZER_PIN, LOW);
   }
   
   delay(10);
 }