/*
 * ========================================================
 *          L-GUARD VEHICLE SAFETY SYSTEM v4.0
 * ========================================================
 * Hardware:
 *  - ESP32 DevKit
 *  - SIM A7670C (Cellular Modem)
 *  - L86M33 GPS Module
 *  - MPU6050 (Gyro + Low-G Accel)
 *  - ADXL377 (High-G Accelerometer)
 *  - LM393 Vibration Sensor
 *  - Push Button (Cancel false alarms)
 *  - LEDs: Safe, Danger, GPS, GSM, System
 *  - Buzzer
 * 
 * Features:
 *  - Multi-sensor accident detection
 *  - Real-time GPS tracking
 *  - Emergency SMS with location
 *  - HTTP data sync via cellular data
 *  - Overspeed warning
 *  - False alarm cancellation (10s window)
 * ========================================================
 */

 #include <Wire.h>
 #include <Adafruit_MPU6050.h>
 #include <Adafruit_Sensor.h>
 #include <TinyGPSPlus.h>
 #include <ArduinoJson.h>
 
 // ==================== PIN DEFINITIONS ====================
 // A7670C Modem
 #define MODEM_RXD 16
 #define MODEM_TXD 17
 
 // L86M33 GPS
 #define GPS_RXD 19
 #define GPS_TXD 18
 
 // I2C for MPU6050 (SDA=21, SCL=22 - default)
 
 // ADXL377 Analog Outputs
 #define ADXL377_X_PIN 36  // VP
 #define ADXL377_Y_PIN 39  // VN
 #define ADXL377_Z_PIN 34
 
 // LM393 Vibration Sensor
 #define VIBRATION_DIGITAL_PIN 35
 #define VIBRATION_ANALOG_PIN 32
 
 // Control & Indicators
 #define CANCEL_BUTTON_PIN 33
 #define SAFE_LED_PIN 2
 #define DANGER_LED_PIN 4
 #define GPS_LED_PIN 5
 #define GSM_LED_PIN 25
 #define SYSTEM_LED_PIN 27
 #define BUZZER_PIN 26
 
 // ==================== CONFIGURATION ====================
 const char* APN = "web.gprs.mtnnigeria.net";
 const char* API_BASE_URL = "http://lguard-backend-service.onrender.com/api";
 String DEVICE_ID = "VEHICLE_001";
 String EMERGENCY_CONTACT = "+250795613644";
 
 // Thresholds
 const float ADXL377_IMPACT_THRESHOLD = 50.0;  // 50g impact
 const float MPU_ACCEL_THRESHOLD = 15.0;       // 15g
 const float GYRO_THRESHOLD = 3.0;              // rad/s
 const int VIBRATION_THRESHOLD = 800;           // Analog value
 const float SPEED_DROP_THRESHOLD = 30.0;       // km/h
 const float OVERSPEED_THRESHOLD = 40.0;        // km/h
 const unsigned long CANCEL_WINDOW = 10000;     // 10 seconds to cancel
 
 // Timing intervals
 const unsigned long GPS_UPDATE_INTERVAL = 1000;
 const unsigned long SENSOR_READ_INTERVAL = 100;
 const unsigned long API_SYNC_INTERVAL = 30000;
 const unsigned long STATUS_PRINT_INTERVAL = 5000;
 
 // ==================== OBJECTS ====================
 Adafruit_MPU6050 mpu;
 TinyGPSPlus gps;
 HardwareSerial modemSerial(2);
 HardwareSerial gpsSerial(1);
 
 // ==================== GLOBAL VARIABLES ====================
 
 // GPS Data
 float currentLat = 0, currentLon = 0, altitude = 0;
 float currentSpeed = 0, previousSpeed = 0;
 bool gpsFixed = false;
 int satellites = 0;
 String utcTime = "";
 
 // Sensor Data
 float mpuAccelX = 0, mpuAccelY = 0, mpuAccelZ = 0, mpuTotalAccel = 0;
 float gyroX = 0, gyroY = 0, gyroZ = 0, totalGyro = 0;
 float adxl377X = 0, adxl377Y = 0, adxl377Z = 0, adxl377Total = 0;
 int vibrationValue = 0;
 bool vibrationDetected = false;
 
 // System State
 bool accidentDetected = false;
 bool smsSent = false;
 bool cancelPressed = false;
 unsigned long accidentTime = 0;
 String accidentType = "";
 
 // Status
 bool modemReady = false;
 bool gprsConnected = false;
 
 // Timing
 unsigned long lastSensorRead = 0;
 unsigned long lastGpsUpdate = 0;
 unsigned long lastApiSync = 0;
 unsigned long lastStatusPrint = 0;
 unsigned long overspeedStartTime = 0;
 bool isOverspeeding = false;
 
 // LED Blink states
 unsigned long lastDangerBlink = 0;
 unsigned long lastGpsBlink = 0;
 unsigned long lastGsmBlink = 0;
 unsigned long lastSystemBlink = 0;
 bool dangerLedState = false;
 bool gpsLedState = false;
 bool gsmLedState = false;
 bool systemLedState = false;
 
 // ==================== FUNCTION PROTOTYPES ====================
 void setupModem();
 void setupGPS();
 void setupSensors();
 void setupPins();
 bool connectGPRS();
 void readSensors();
 void readGPS();
 void checkAccident();
 void handleCancelButton();
 void sendEmergencySMS();
 void sendAccidentToAPI();
 void sendTrackingDataToAPI();
 void updateStatusLEDs();
 void handleOverspeed();
 String sendATCommand(String cmd, unsigned long timeout = 1000);
 String readModemResponse(unsigned long timeout = 2000);
 bool httpPOST(String endpoint, String jsonData);
 
 // ==================== SETUP ====================
 void setup() {
   Serial.begin(115200);
   delay(2000);
   
   Serial.println("\n\n");
   Serial.println("=========================================================");
   Serial.println("         🛡️  L-GUARD VEHICLE SAFETY SYSTEM v4.0");
   Serial.println("                  Cellular Data Edition");
   Serial.println("=========================================================");
   Serial.println("Hardware:");
   Serial.println("  ✓ SIM A7670C (4G LTE)");
   Serial.println("  ✓ L86M33 GPS");
   Serial.println("  ✓ MPU6050 (Gyro + Accel)");
   Serial.println("  ✓ ADXL377 (High-G Impact Sensor)");
   Serial.println("  ✓ LM393 (Vibration Sensor)");
   Serial.println("  ✓ Cancel Button (10s window)");
   Serial.println("=========================================================\n");
   
   setupPins();
   
   // Boot animation
   Serial.println("🔄 Initializing system...");
   for(int i = 0; i < 3; i++) {
     digitalWrite(SAFE_LED_PIN, HIGH);
     digitalWrite(DANGER_LED_PIN, HIGH);
     digitalWrite(GPS_LED_PIN, HIGH);
     digitalWrite(GSM_LED_PIN, HIGH);
     digitalWrite(SYSTEM_LED_PIN, HIGH);
     delay(200);
     digitalWrite(SAFE_LED_PIN, LOW);
     digitalWrite(DANGER_LED_PIN, LOW);
     digitalWrite(GPS_LED_PIN, LOW);
     digitalWrite(GSM_LED_PIN, LOW);
     digitalWrite(SYSTEM_LED_PIN, LOW);
     delay(200);
   }
   
   setupSensors();
   setupGPS();
   setupModem();
   
   if(modemReady) {
     if(connectGPRS()) {
       Serial.println("✅ System fully operational!\n");
       digitalWrite(SYSTEM_LED_PIN, HIGH);
     } else {
       Serial.println("⚠️  GPRS connection failed - SMS only mode\n");
       digitalWrite(SYSTEM_LED_PIN, LOW);
     }
   }
   
   digitalWrite(SAFE_LED_PIN, HIGH);
   
   Serial.println("=========================================================");
   Serial.println("🚨 ACCIDENT DETECTION: ACTIVE");
   Serial.println("🛰️  GPS TRACKING: ACTIVE");
   Serial.println("📱 SMS EMERGENCY: READY");
   Serial.println("☁️  API SYNC: " + String(gprsConnected ? "ENABLED" : "DISABLED"));
   Serial.println("🔊 OVERSPEED WARNING: >40 km/h");
   Serial.println("🛑 CANCEL BUTTON: 10s window after detection");
   Serial.println("=========================================================\n");
 }
 
 // ==================== MAIN LOOP ====================
 void loop() {
   unsigned long currentMillis = millis();
   
   // Read sensors at high frequency
   if(currentMillis - lastSensorRead >= SENSOR_READ_INTERVAL) {
     readSensors();
     checkAccident();
     lastSensorRead = currentMillis;
   }
   
   // Update GPS
   if(currentMillis - lastGpsUpdate >= GPS_UPDATE_INTERVAL) {
     readGPS();
     handleOverspeed();
     lastGpsUpdate = currentMillis;
   }
   
   // Sync to API periodically
   if(gprsConnected && currentMillis - lastApiSync >= API_SYNC_INTERVAL) {
     sendTrackingDataToAPI();
     lastApiSync = currentMillis;
   }
   
   // Handle cancel button
   if(accidentDetected && !smsSent) {
     handleCancelButton();
   }
   
   // Update status LEDs
   updateStatusLEDs();
   
   // Print status
   if(currentMillis - lastStatusPrint >= STATUS_PRINT_INTERVAL) {
     if(!accidentDetected) {
       Serial.print("✅ SAFE | ADXL377:");
       Serial.print(adxl377Total, 1);
       Serial.print("g | MPU:");
       Serial.print(mpuTotalAccel, 1);
       Serial.print("g | Gyro:");
       Serial.print(totalGyro, 1);
       Serial.print(" | Vib:");
       Serial.print(vibrationValue);
       Serial.print(" | Speed:");
       Serial.print(currentSpeed, 1);
       Serial.print("km/h | GPS:");
       Serial.println(gpsFixed ? "FIXED" : "NO FIX");
     }
     lastStatusPrint = currentMillis;
   }
   
   delay(10);
 }
 
 // ==================== PIN SETUP ====================
 void setupPins() {
   pinMode(SAFE_LED_PIN, OUTPUT);
   pinMode(DANGER_LED_PIN, OUTPUT);
   pinMode(GPS_LED_PIN, OUTPUT);
   pinMode(GSM_LED_PIN, OUTPUT);
   pinMode(SYSTEM_LED_PIN, OUTPUT);
   pinMode(BUZZER_PIN, OUTPUT);
   pinMode(CANCEL_BUTTON_PIN, INPUT_PULLUP);
   pinMode(VIBRATION_DIGITAL_PIN, INPUT);
   
   // Turn off all LEDs
   digitalWrite(SAFE_LED_PIN, LOW);
   digitalWrite(DANGER_LED_PIN, LOW);
   digitalWrite(GPS_LED_PIN, LOW);
   digitalWrite(GSM_LED_PIN, LOW);
   digitalWrite(SYSTEM_LED_PIN, LOW);
   digitalWrite(BUZZER_PIN, LOW);
 }
 
 // ==================== SENSOR SETUP ====================
 void setupSensors() {
   Serial.println("🔧 Initializing sensors...");
   
   // Initialize I2C
   Wire.begin(21, 22);
   
   // Initialize MPU6050
   Serial.print("  • MPU6050... ");
   if (!mpu.begin()) {
     Serial.println("❌ FAILED!");
     while(1) {
       digitalWrite(DANGER_LED_PIN, HIGH);
       delay(200);
       digitalWrite(DANGER_LED_PIN, LOW);
       delay(200);
     }
   }
   Serial.println("✅");
   
   mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
   mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
   mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
   
   // ADXL377 is analog - no initialization needed
   Serial.println("  • ADXL377 (Analog)... ✅");
   
   // LM393 is digital/analog - no initialization needed
   Serial.println("  • LM393 Vibration... ✅");
   
   Serial.println("✅ All sensors initialized!\n");
 }
 
 // ==================== GPS SETUP ====================
 void setupGPS() {
   Serial.println("🛰️  Initializing GPS...");
   gpsSerial.begin(9600, SERIAL_8N1, GPS_RXD, GPS_TXD);
   Serial.println("✅ L86M33 GPS started on pins 19(RX), 18(TX)\n");
   delay(1000);
 }
 
 // ==================== MODEM SETUP ====================
 void setupModem() {
   Serial.println("📱 Initializing SIM A7670C...");
   
   modemSerial.begin(115200, SERIAL_8N1, MODEM_RXD, MODEM_TXD);
   delay(2000);
   
   // Test modem
   sendATCommand("AT", 500);
   sendATCommand("ATE0", 500);  // Echo off
   
   // Check SIM
   String simResp = sendATCommand("AT+CPIN?", 2000);
   if(simResp.indexOf("READY") >= 0) {
     Serial.println("  ✅ SIM Card Ready");
   } else {
     Serial.println("  ❌ SIM Card Error!");
     return;
   }
   
   // Check signal
   String signalResp = sendATCommand("AT+CSQ", 1000);
   Serial.println("  📶 Signal: " + signalResp);
   
   // Check network registration
   String netResp = sendATCommand("AT+CREG?", 2000);
   if(netResp.indexOf("+CREG: 0,1") >= 0 || netResp.indexOf("+CREG: 0,5") >= 0) {
     Serial.println("  ✅ Network Registered");
     modemReady = true;
   } else {
     Serial.println("  ⚠️  Network Registration Pending...");
     delay(3000);
     netResp = sendATCommand("AT+CREG?", 2000);
     if(netResp.indexOf("+CREG: 0,1") >= 0 || netResp.indexOf("+CREG: 0,5") >= 0) {
       Serial.println("  ✅ Network Registered");
       modemReady = true;
     }
   }
   
   // Set SMS mode to text
   sendATCommand("AT+CMGF=1", 1000);
   
   Serial.println("✅ Modem initialized!\n");
 }
 
 // ==================== GPRS CONNECTION ====================
 bool connectGPRS() {
   Serial.println("🌐 Connecting to GPRS...");
   
   // Deactivate old context
   sendATCommand("AT+CGACT=0,1", 2000);
   delay(500);
   
   // Set APN
   sendATCommand("AT+CGDCONT=1,\"IP\",\"" + String(APN) + "\"", 2000);
   
   // Activate PDP context
   String activateResp = sendATCommand("AT+CGACT=1,1", 10000);
   
   if(activateResp.indexOf("OK") >= 0) {
     // Check IP address
     String ipResp = sendATCommand("AT+CGPADDR=1", 2000);
     
     if(ipResp.indexOf("+CGPADDR") >= 0) {
       Serial.println("✅ GPRS Connected!");
       Serial.println("  IP: " + ipResp);
       gprsConnected = true;
       return true;
     }
   }
   
   Serial.println("❌ GPRS Connection Failed");
   gprsConnected = false;
   return false;
 }
 
 // ==================== READ SENSORS ====================
 void readSensors() {
   // Read MPU6050
   sensors_event_t a, g, temp;
   mpu.getEvent(&a, &g, &temp);
   
   mpuAccelX = a.acceleration.x;
   mpuAccelY = a.acceleration.y;
   mpuAccelZ = a.acceleration.z;
   mpuTotalAccel = sqrt(mpuAccelX*mpuAccelX + mpuAccelY*mpuAccelY + mpuAccelZ*mpuAccelZ);
   
   gyroX = g.gyro.x;
   gyroY = g.gyro.y;
   gyroZ = g.gyro.z;
   totalGyro = sqrt(gyroX*gyroX + gyroY*gyroY + gyroZ*gyroZ);
   
   // Read ADXL377 (High-G Accelerometer)
   // ADXL377: 3.3V supply, ~200g range, ~6.5mV/g sensitivity
   // ADC: 12-bit (0-4095), VREF = 3.3V
   // Zero-g offset ≈ 1.65V ≈ 2048 counts
   
   int rawX = analogRead(ADXL377_X_PIN);
   int rawY = analogRead(ADXL377_Y_PIN);
   int rawZ = analogRead(ADXL377_Z_PIN);
   
   // Convert to g (adjust calibration as needed)
   adxl377X = (rawX - 2048) * (3.3 / 4095.0) / 0.0065;  // 6.5mV/g
   adxl377Y = (rawY - 2048) * (3.3 / 4095.0) / 0.0065;
   adxl377Z = (rawZ - 2048) * (3.3 / 4095.0) / 0.0065;
   
   adxl377Total = sqrt(adxl377X*adxl377X + adxl377Y*adxl377Y + adxl377Z*adxl377Z);
   
   // Read LM393 Vibration Sensor
   vibrationDetected = digitalRead(VIBRATION_DIGITAL_PIN) == LOW;  // Active LOW
   vibrationValue = analogRead(VIBRATION_ANALOG_PIN);
 }
 
 // ==================== READ GPS ====================
 void readGPS() {
   while(gpsSerial.available() > 0) {
     char c = gpsSerial.read();
     gps.encode(c);
   }
   
   if(gps.location.isUpdated()) {
     currentLat = gps.location.lat();
     currentLon = gps.location.lng();
     gpsFixed = gps.location.isValid();
   }
   
   if(gps.altitude.isUpdated()) {
     altitude = gps.altitude.meters();
   }
   
   if(gps.speed.isUpdated()) {
     previousSpeed = currentSpeed;
     currentSpeed = gps.speed.kmph();
   }
   
   if(gps.satellites.isUpdated()) {
     satellites = gps.satellites.value();
   }
   
   if(gps.time.isValid()) {
     char timeStr[16];
     sprintf(timeStr, "%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
     utcTime = String(timeStr);
   }
 }
 
 // ==================== ACCIDENT DETECTION ====================
 void checkAccident() {
   if(accidentDetected) return;  // Already detected
   
   bool highGImpact = (adxl377Total > ADXL377_IMPACT_THRESHOLD);
   bool mpuImpact = (mpuTotalAccel > MPU_ACCEL_THRESHOLD);
   bool gyroImpact = (totalGyro > GYRO_THRESHOLD);
   bool vibrationImpact = (vibrationValue > VIBRATION_THRESHOLD || vibrationDetected);
   bool speedDrop = false;
   
   // Check for sudden speed drop
   if(gpsFixed && previousSpeed > 40.0 && currentSpeed < 10.0) {
     float drop = previousSpeed - currentSpeed;
     if(drop >= SPEED_DROP_THRESHOLD) {
       speedDrop = true;
     }
   }
   
   // Accident conditions (multiple sensors for reliability)
   bool accidentCondition = false;
   
   if(highGImpact) {
     accidentType = "HIGH_G_IMPACT";
     accidentCondition = true;
   } else if(mpuImpact && vibrationImpact) {
     accidentType = "IMPACT_VIBRATION";
     accidentCondition = true;
   } else if(speedDrop && vibrationImpact) {
     accidentType = "SPEED_DROP_COLLISION";
     accidentCondition = true;
   } else if(gyroImpact && mpuImpact) {
     accidentType = "ROLLOVER_IMPACT";
     accidentCondition = true;
   }
   
   if(accidentCondition) {
     accidentDetected = true;
     accidentTime = millis();
     cancelPressed = false;
     
     // Turn off safe LED, start danger blinking
     digitalWrite(SAFE_LED_PIN, LOW);
     
     // Sound buzzer pattern
     for(int i = 0; i < 5; i++) {
       digitalWrite(BUZZER_PIN, HIGH);
       delay(100);
       digitalWrite(BUZZER_PIN, LOW);
       delay(100);
     }
     
     Serial.println("\n");
     Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
     Serial.println("        ⚠️  ACCIDENT DETECTED! ⚠️");
     Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
     Serial.println("Type: " + accidentType);
     Serial.println("Time: " + utcTime);
     Serial.println("ADXL377: " + String(adxl377Total, 1) + "g");
     Serial.println("MPU6050: " + String(mpuTotalAccel, 1) + "g");
     Serial.println("Gyro: " + String(totalGyro, 1) + " rad/s");
     Serial.println("Vibration: " + String(vibrationValue));
     Serial.println("Speed: " + String(currentSpeed, 1) + " km/h");
     if(gpsFixed) {
       Serial.println("Location: " + String(currentLat, 6) + ", " + String(currentLon, 6));
     }
     Serial.println("\n🛑 Press CANCEL button within 10s to abort alert!");
     Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨\n");
   }
 }
 
 // ==================== HANDLE CANCEL BUTTON ====================
 void handleCancelButton() {
   unsigned long elapsed = millis() - accidentTime;
   
   if(digitalRead(CANCEL_BUTTON_PIN) == LOW) {  // Button pressed
     delay(50);  // Debounce
     if(digitalRead(CANCEL_BUTTON_PIN) == LOW) {
       cancelPressed = true;
       
       Serial.println("\n✋ ALERT CANCELLED BY USER!");
       Serial.println("Resetting system...\n");
       
       accidentDetected = false;
       smsSent = false;
       digitalWrite(DANGER_LED_PIN, LOW);
       digitalWrite(SAFE_LED_PIN, HIGH);
       digitalWrite(BUZZER_PIN, LOW);
       
       // Wait for button release
       while(digitalRead(CANCEL_BUTTON_PIN) == LOW) {
         delay(10);
       }
     }
   }
   
   // Auto-send after 10 seconds if not cancelled
   if(elapsed >= CANCEL_WINDOW && !cancelPressed && !smsSent) {
     Serial.println("⏰ 10s elapsed - Sending emergency alerts!");
     sendEmergencySMS();
     
     if(gprsConnected) {
       sendAccidentToAPI();
     }
   }
 }
 
 // ==================== SEND EMERGENCY SMS ====================
 void sendEmergencySMS() {
   if(smsSent) return;
   
   Serial.println("\n📱 =======================================");
   Serial.println("      SENDING EMERGENCY SMS");
   Serial.println("=======================================");
   
   // Set SMS mode
   sendATCommand("AT+CMGF=1", 1000);
   
   // Set phone number
   modemSerial.print("AT+CMGS=\"");
   modemSerial.print(EMERGENCY_CONTACT);
   modemSerial.println("\"");
   delay(500);
   
   // Compose message
   String message = "🚨 VEHICLE ACCIDENT DETECTED! 🚨\n\n";
   message += "Device: " + DEVICE_ID + "\n";
   message += "Type: " + accidentType + "\n";
   message += "Time: " + utcTime + " UTC\n\n";
   
   if(gpsFixed) {
     message += "📍 LOCATION:\n";
     message += "Lat: " + String(currentLat, 6) + "\n";
     message += "Lon: " + String(currentLon, 6) + "\n";
     message += "Speed: " + String(currentSpeed, 1) + " km/h\n\n";
     message += "🗺️ Google Maps:\n";
     message += "https://maps.google.com/?q=" + String(currentLat, 6) + "," + String(currentLon, 6);
   } else {
     message += "⚠️ GPS: No location available\n";
     message += "Please call immediately!";
   }
   
   message += "\n\n⚠️ EMERGENCY ASSISTANCE NEEDED ⚠️";
   
   // Send message
   modemSerial.print(message);
   delay(500);
   modemSerial.write(26);  // Ctrl+Z
   
   // Wait for response
   delay(5000);
   String response = readModemResponse(10000);
   
   if(response.indexOf("+CMGS:") >= 0 || response.indexOf("OK") >= 0) {
     Serial.println("✅ EMERGENCY SMS SENT SUCCESSFULLY!");
     smsSent = true;
   } else {
     Serial.println("❌ SMS Send Failed - Retrying...");
     delay(2000);
     
     // Retry once
     modemSerial.print("AT+CMGS=\"");
     modemSerial.print(EMERGENCY_CONTACT);
     modemSerial.println("\"");
     delay(500);
     modemSerial.print(message);
     delay(500);
     modemSerial.write(26);
     delay(5000);
     response = readModemResponse(10000);
     
     if(response.indexOf("+CMGS:") >= 0 || response.indexOf("OK") >= 0) {
       Serial.println("✅ EMERGENCY SMS SENT (Retry)!");
       smsSent = true;
     } else {
       Serial.println("❌ SMS Send Failed After Retry");
     }
   }
   
   Serial.println("=======================================\n");
 }
 
 // ==================== SEND ACCIDENT TO API ====================
 void sendAccidentToAPI() {
   Serial.println("☁️  Sending accident data to API...");
   
   DynamicJsonDocument doc(2048);
   doc["device_id"] = DEVICE_ID;
   doc["accident_type"] = accidentType;
   doc["severity"] = "HIGH";
   doc["timestamp"] = utcTime;
   
   // Location
   if(gpsFixed) {
     JsonObject location = doc.createNestedObject("location");
     location["latitude"] = currentLat;
     location["longitude"] = currentLon;
     location["altitude"] = altitude;
     location["speed"] = currentSpeed;
     location["satellites"] = satellites;
   }
   
   // Impact data
   JsonObject impact = doc.createNestedObject("impact_data");
   impact["adxl377_g"] = adxl377Total;
   impact["mpu_accel_g"] = mpuTotalAccel;
   impact["gyro_rads"] = totalGyro;
   impact["vibration"] = vibrationValue;
   impact["speed_before"] = previousSpeed;
   impact["speed_after"] = currentSpeed;
   
   // Emergency
   JsonObject emergency = doc.createNestedObject("emergency");
   emergency["contact"] = EMERGENCY_CONTACT;
   emergency["sms_sent"] = smsSent;
   
   String jsonData;
   serializeJson(doc, jsonData);
   
   if(httpPOST("/accidents", jsonData)) {
     Serial.println("✅ Accident logged to API");
   } else {
     Serial.println("❌ API logging failed");
   }
 }
 
 // ==================== SEND TRACKING DATA ====================
 void sendTrackingDataToAPI() {
   DynamicJsonDocument doc(1536);
   doc["device_id"] = DEVICE_ID;
   doc["timestamp"] = utcTime;
   
   // GPS
   if(gpsFixed) {
     JsonObject location = doc.createNestedObject("location");
     location["latitude"] = currentLat;
     location["longitude"] = currentLon;
     location["altitude"] = altitude;
     location["speed"] = currentSpeed;
     location["satellites"] = satellites;
   }
   
   // Sensors
   JsonObject sensors = doc.createNestedObject("sensors");
   sensors["adxl377_total_g"] = adxl377Total;
   sensors["mpu_accel_g"] = mpuTotalAccel;
   sensors["gyro_rads"] = totalGyro;
   sensors["vibration"] = vibrationValue;
   
   // Status
   JsonObject status = doc.createNestedObject("status");
   status["gsm_ready"] = modemReady;
   status["gps_fixed"] = gpsFixed;
   status["gprs_connected"] = gprsConnected;
   
   String jsonData;
   serializeJson(doc, jsonData);
   
   if(httpPOST("/tracking", jsonData)) {
     Serial.println("✅ Tracking data synced");
   }
 }
 
 // ==================== HTTP POST ====================
 bool httpPOST(String endpoint, String jsonData) {
   if(!gprsConnected) return false;
   
   String url = String(API_BASE_URL) + endpoint;
   
   // HTTP configuration
   sendATCommand("AT+HTTPTERM", 1000);
   sendATCommand("AT+HTTPINIT", 2000);
   sendATCommand("AT+HTTPPARA=\"CID\",1", 1000);
   sendATCommand("AT+HTTPPARA=\"URL\",\"" + url + "\"", 1000);
   sendATCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 1000);
   
   // Send data
   modemSerial.println("AT+HTTPDATA=" + String(jsonData.length()) + ",10000");
   delay(500);
   String dataResp = readModemResponse(1000);
   
   if(dataResp.indexOf("DOWNLOAD") >= 0) {
     modemSerial.print(jsonData);
     delay(1000);
     
     // Execute POST
     modemSerial.println("AT+HTTPACTION=1");
     String actionResp = readModemResponse(15000);
     
     if(actionResp.indexOf("+HTTPACTION: 1,200") >= 0 || 
        actionResp.indexOf("+HTTPACTION: 1,201") >= 0) {
       sendATCommand("AT+HTTPTERM", 1000);
       return true;
     }
   }
   
   sendATCommand("AT+HTTPTERM", 1000);
   return false;
 }
 
 // ==================== HANDLE OVERSPEED ====================
 void handleOverspeed() {
   if(gpsFixed && currentSpeed > OVERSPEED_THRESHOLD) {
     if(!isOverspeeding) {
       isOverspeeding = true;
       overspeedStartTime = millis();
       Serial.println("⚠️  OVERSPEED: " + String(currentSpeed, 1) + " km/h");
     }
     
     // Beep buzzer
     static unsigned long lastBeep = 0;
     if(millis() - lastBeep >= 1000) {
       digitalWrite(BUZZER_PIN, HIGH);
       delay(100);
       digitalWrite(BUZZER_PIN, LOW);
       lastBeep = millis();
     }
   } else {
     if(isOverspeeding) {
       unsigned long duration = (millis() - overspeedStartTime) / 1000;
       Serial.println("✅ Speed normalized. Duration: " + String(duration) + "s");
       isOverspeeding = false;
     }
   }
 }
 
 // ==================== UPDATE STATUS LEDS ====================
 void updateStatusLEDs() {
   unsigned long currentMillis = millis();
   
   // Danger LED (Fast blink when accident detected)
   if(accidentDetected && !smsSent) {
     if(currentMillis - lastDangerBlink >= 200) {
       dangerLedState = !dangerLedState;
       digitalWrite(DANGER_LED_PIN, dangerLedState);
       lastDangerBlink = currentMillis;
     }
   } else if(smsSent) {
     digitalWrite(DANGER_LED_PIN, HIGH);  // Solid when SMS sent
   }
   
   // GPS LED (Slow blink = no fix, fast = fixed)
   unsigned long gpsInterval = gpsFixed ? 500 : 1000;
   if(currentMillis - lastGpsBlink >= gpsInterval) {
     gpsLedState = !gpsLedState;
     digitalWrite(GPS_LED_PIN, gpsLedState);
     lastGpsBlink = currentMillis;
   }
   
   // GSM LED (Slow blink = searching, fast = connected)
   unsigned long gsmInterval = modemReady ? 300 : 1500;
   if(currentMillis - lastGsmBlink >= gsmInterval) {
     gsmLedState = !gsmLedState;
     digitalWrite(GSM_LED_PIN, gsmLedState);
     lastGsmBlink = currentMillis;
   }
   
   // System LED (Solid = GPRS connected, blink = disconnected)
   if(gprsConnected) {
     digitalWrite(SYSTEM_LED_PIN, HIGH);
   } else {
     if(currentMillis - lastSystemBlink >= 2000) {
       systemLedState = !systemLedState;
       digitalWrite(SYSTEM_LED_PIN, systemLedState);
       lastSystemBlink = currentMillis;
     }
   }
 }
 
 // ==================== AT COMMAND HELPERS ====================
 String sendATCommand(String cmd, unsigned long timeout) {
   Serial.print(">> ");
   Serial.println(cmd);
   
   modemSerial.println(cmd);
   String response = readModemResponse(timeout);
   
   Serial.print("<< ");
   Serial.println(response);
   
   return response;
 }
 
 String readModemResponse(unsigned long timeout) {
   String response = "";
   unsigned long start = millis();
   
   while(millis() - start < timeout) {
     if(modemSerial.available()) {
       char c = modemSerial.read();
       response += c;
     }
     
     // Check for common response endings
     if(response.indexOf("OK\r\n") >= 0 || 
        response.indexOf("ERROR\r\n") >= 0 ||
        response.indexOf("+CMGS:") >= 0 ||
        response.indexOf("+HTTPACTION:") >= 0) {
       break;
     }
   }
   
   response.trim();
   return response;
 }