#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

Adafruit_MPU6050 mpu;
Preferences preferences;
DNSServer dnsServer;
WebServer server(80);

// WiFi Configuration Portal
bool configMode = false;
const char* apSSID = "L-Guard-Setup";
const char* apPassword = "12345678";
String savedSSID = "";
String savedPassword = "";

// API Configuration
const char* apiBaseUrl = "https://lguard-backend-service.onrender.com/api";
String apiToken = ""; // Will be set after authentication if needed

// Device ID
String deviceId = "VEHICLE_001";

// Hardware Serial for GPS and GSM
HardwareSerial gpsSerial(1);
HardwareSerial gsmSerial(2);

// Pin definitions
const int piezoPin = 34;
const int safeLED = 2;
const int dangerLED = 4;
const int gpsLED = 5;
const int gsmLED = 25;
const int buzzerPin = 26;
const int wifiLED = 27;
const int configButton = 0; // Boot button for reset

// Emergency contact 
String phoneNumber = "+250795613644";

// GPS variables
float currentLat = 0, currentLon = 0, altitude = 0;
float currentSpeed = 0;
float previousSpeed = 0;
bool gpsFixed = false;
String gpsBuffer = "";
int satellitesInUse = 0;
String utcTime = "";
String fixQuality = "No Fix";

// Sensor variables
float accelX = 0, accelY = 0, accelZ = 0, totalAccel = 0;
float gyroX = 0, gyroY = 0, gyroZ = 0, totalGyro = 0;
int piezoValue = 0;

// Thresholds
const float ACCEL_THRESHOLD = 15.0;
const float GYRO_THRESHOLD = 3.0;
const int PIEZO_THRESHOLD = 1200;
const float SPEED_DROP_THRESHOLD = 30.0;
const float LOW_SPEED_THRESHOLD = 10.0;
const float OVERSPEED_THRESHOLD = 40.0;

// State variables
bool accidentDetected = false;
bool smsSent = false;
unsigned long accidentTime = 0;
unsigned long lastSpeedCheck = 0;
unsigned long blinkStartTime = 0;
bool isBlinking = false;
bool ledState = false;
unsigned long lastBlinkTime = 0;
unsigned long lastGpsBlinkTime = 0;
unsigned long lastGsmBlinkTime = 0;
unsigned long lastWifiBlinkTime = 0;
bool gpsLedState = false;
bool gsmLedState = false;
bool wifiLedState = false;
bool gsmReady = false;
bool wifiConnected = false;
unsigned long lastBuzzerTime = 0;
bool buzzerState = false;
unsigned long overspeedStartTime = 0;
bool isOverspeeding = false;
unsigned long lastDataSend = 0;
const unsigned long DATA_SEND_INTERVAL = 30000;

// HTML Page for WiFi Configuration
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>L-Guard WiFi Setup</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .container {
            background: white;
            border-radius: 20px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            padding: 40px;
            max-width: 500px;
            width: 100%;
        }
        .logo {
            text-align: center;
            margin-bottom: 30px;
        }
        .logo h1 {
            color: #667eea;
            font-size: 32px;
            margin-bottom: 5px;
        }
        .logo p {
            color: #666;
            font-size: 14px;
        }
        .form-group {
            margin-bottom: 25px;
        }
        label {
            display: block;
            margin-bottom: 8px;
            color: #333;
            font-weight: 600;
            font-size: 14px;
        }
        input[type="text"], input[type="password"], select {
            width: 100%;
            padding: 12px 15px;
            border: 2px solid #e0e0e0;
            border-radius: 10px;
            font-size: 16px;
            transition: all 0.3s;
        }
        input:focus, select:focus {
            outline: none;
            border-color: #667eea;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
        }
        .btn {
            width: 100%;
            padding: 15px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 10px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: transform 0.2s;
        }
        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 20px rgba(102, 126, 234, 0.4);
        }
        .btn:active {
            transform: translateY(0);
        }
        .info {
            background: #f0f4ff;
            padding: 15px;
            border-radius: 10px;
            margin-bottom: 20px;
            font-size: 14px;
            color: #555;
        }
        .status {
            text-align: center;
            margin-top: 20px;
            padding: 15px;
            border-radius: 10px;
            display: none;
        }
        .status.success {
            background: #d4edda;
            color: #155724;
            display: block;
        }
        .status.error {
            background: #f8d7da;
            color: #721c24;
            display: block;
        }
        .loading {
            display: none;
            text-align: center;
            margin-top: 20px;
        }
        .spinner {
            border: 3px solid #f3f3f3;
            border-top: 3px solid #667eea;
            border-radius: 50%;
            width: 40px;
            height: 40px;
            animation: spin 1s linear infinite;
            margin: 0 auto;
        }
        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="logo">
            <h1>🛡️ L-Guard</h1>
            <p>Vehicle Safety System - WiFi Setup</p>
        </div>
        
        <div class="info">
            ℹ️ Connect your L-Guard device to your WiFi network for cloud sync and real-time tracking.
        </div>
        
        <form id="wifiForm">
            <div class="form-group">
                <label for="ssid">WiFi Network Name (SSID)</label>
                <input type="text" id="ssid" name="ssid" placeholder="Enter your WiFi name" required>
            </div>
            
            <div class="form-group">
                <label for="password">WiFi Password</label>
                <input type="password" id="password" name="password" placeholder="Enter your WiFi password" required>
            </div>
            
            <div class="form-group">
                <label for="device_id">Device ID (Optional)</label>
                <input type="text" id="device_id" name="device_id" placeholder="VEHICLE_001" value="VEHICLE_001">
            </div>
            
            <button type="submit" class="btn">💾 Save & Connect</button>
        </form>
        
        <div class="loading" id="loading">
            <div class="spinner"></div>
            <p style="margin-top: 10px;">Connecting to WiFi...</p>
        </div>
        
        <div class="status" id="status"></div>
    </div>
    
    <script>
        document.getElementById('wifiForm').addEventListener('submit', async (e) => {
            e.preventDefault();
            
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;
            const device_id = document.getElementById('device_id').value;
            
            document.getElementById('loading').style.display = 'block';
            document.getElementById('status').style.display = 'none';
            
            try {
                const response = await fetch('/save', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: `ssid=${encodeURIComponent(ssid)}&password=${encodeURIComponent(password)}&device_id=${encodeURIComponent(device_id)}`
                });
                
                const result = await response.text();
                
                document.getElementById('loading').style.display = 'none';
                const statusDiv = document.getElementById('status');
                
                if (result.includes('Success')) {
                    statusDiv.className = 'status success';
                    statusDiv.innerHTML = '✅ WiFi settings saved! Device will restart and connect to your network.';
                    setTimeout(() => {
                        statusDiv.innerHTML += '<br><br>You can close this page now.';
                    }, 2000);
                } else {
                    statusDiv.className = 'status error';
                    statusDiv.innerHTML = '❌ Failed to save settings. Please try again.';
                }
            } catch (error) {
                document.getElementById('loading').style.display = 'none';
                const statusDiv = document.getElementById('status');
                statusDiv.className = 'status error';
                statusDiv.innerHTML = '❌ Connection error. Please try again.';
            }
        });
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  Serial.println("=======================================================");
  Serial.println("         🛡️  L-GUARD VEHICLE SAFETY SYSTEM");
  Serial.println("              Production Version 3.0");
  Serial.println("          Cloud Connected + WiFi Manager");
  Serial.println("             API: lguard-backend-service");
  Serial.println("=======================================================\n");
  
  // Initialize pins
  pinMode(safeLED, OUTPUT);
  pinMode(dangerLED, OUTPUT);
  pinMode(gpsLED, OUTPUT);
  pinMode(gsmLED, OUTPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(wifiLED, OUTPUT);
  pinMode(configButton, INPUT_PULLUP);
  
  // Boot animation
  for(int i = 0; i < 3; i++) {
    digitalWrite(safeLED, HIGH);
    digitalWrite(dangerLED, HIGH);
    digitalWrite(gpsLED, HIGH);
    digitalWrite(gsmLED, HIGH);
    digitalWrite(wifiLED, HIGH);
    delay(200);
    digitalWrite(safeLED, LOW);
    digitalWrite(dangerLED, LOW);
    digitalWrite(gpsLED, LOW);
    digitalWrite(gsmLED, LOW);
    digitalWrite(wifiLED, LOW);
    delay(200);
  }
  
  // Initialize preferences
  preferences.begin("lguard", false);
  
  // Check if reset button is pressed (hold for 3 seconds)
  Serial.println("🔘 Hold BOOT button for 3s to reset WiFi...");
  unsigned long resetStart = millis();
  bool resetPressed = true;
  while(millis() - resetStart < 3000) {
    if(digitalRead(configButton) == HIGH) {
      resetPressed = false;
      break;
    }
    delay(100);
  }
  
  if(resetPressed) {
    Serial.println("🔄 Resetting WiFi credentials...");
    preferences.clear();
    digitalWrite(dangerLED, HIGH);
    delay(1000);
    digitalWrite(dangerLED, LOW);
    Serial.println("✅ WiFi credentials cleared!");
  }
  
  // Load saved credentials
  savedSSID = preferences.getString("ssid", "");
  savedPassword = preferences.getString("password", "");
  deviceId = preferences.getString("device_id", "VEHICLE_001");
  
  Serial.println("📱 Device ID: " + deviceId);
  
  // Initialize I2C for MPU6050
  Wire.begin(21, 22);
  
  // Initialize MPU6050
  Serial.print("🔧 Initializing MPU6050... ");
  if (!mpu.begin()) {
    Serial.println("❌ Failed!");
    while (1) {
      digitalWrite(dangerLED, HIGH);
      delay(200);
      digitalWrite(dangerLED, LOW);
      delay(200);
    }
  }
  Serial.println("✅ Connected!");
  
  mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
  mpu.setGyroRange(MPU6050_RANGE_2000_DEG);
  
  // Try to connect to WiFi
  if (savedSSID.length() > 0) {
    Serial.println("\n🌐 Connecting to saved WiFi: " + savedSSID);
    connectToWiFi(savedSSID, savedPassword);
  } else {
    Serial.println("\n⚠️  No WiFi credentials saved!");
    startConfigPortal();
  }
  
  // If WiFi failed, start config portal
  if (!wifiConnected) {
    Serial.println("❌ WiFi connection failed!");
    startConfigPortal();
  }
  
  // Initialize GPS
  gpsSerial.begin(9600, SERIAL_8N1, 19, 18);
  Serial.println("✅ GPS initialized on pins 19(RX), 18(TX)");
  delay(1000);
  
  // Initialize GSM
  gsmSerial.begin(9600, SERIAL_8N1, 16, 17);
  Serial.println("✅ GSM initialized on pins 16(RX), 17(TX)");
  delay(3000);
  
  testGSM();
  
  // Register device with API
  if (wifiConnected) {
    registerDeviceWithAPI();
  }
  
  // Initial LED states
  digitalWrite(safeLED, HIGH);
  digitalWrite(dangerLED, LOW);
  
  Serial.println("\n✨ SYSTEM READY");
  Serial.println("🚨 Accident Detection: Active");
  Serial.println("🛰️  GPS Tracking: Started");
  Serial.println("📱 GSM Communication: Ready");
  Serial.println("🔊 Overspeed Warning: Active (>40km/h)");
  if(wifiConnected) {
    Serial.println("☁️  API Cloud Sync: Enabled");
  } else {
    Serial.println("📡 Running in Config Mode");
  }
  Serial.println("=======================================================\n");
}

void loop() {
  // Handle config mode
  if (configMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    
    // Blink WiFi LED in config mode
    if (millis() - lastWifiBlinkTime >= 200) {
      wifiLedState = !wifiLedState;
      digitalWrite(wifiLED, wifiLedState);
      lastWifiBlinkTime = millis();
    }
    return;
  }
  
  // Normal operation
  handleGPS();
  
  if (gsmSerial.available()) {
    String gsmData = gsmSerial.readString();
  }
  
  handleOverspeedWarning();
  performAccidentDetection();
  updateStatusLEDs();
  
  if (wifiConnected && (millis() - lastDataSend >= DATA_SEND_INTERVAL)) {
    sendTrackingDataToAPI();
    lastDataSend = millis();
  }
  
  delay(100);
}

void connectToWiFi(String ssid, String password) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Blink WiFi LED while connecting
    digitalWrite(wifiLED, attempts % 2);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    digitalWrite(wifiLED, HIGH);
    Serial.println("\n✅ WiFi Connected!");
    Serial.println("📡 IP Address: " + WiFi.localIP().toString());
    Serial.println("📶 Signal: " + String(WiFi.RSSI()) + " dBm");
  } else {
    wifiConnected = false;
    digitalWrite(wifiLED, LOW);
    Serial.println("\n❌ WiFi Connection Failed");
  }
}

void startConfigPortal() {
  configMode = true;
  
  Serial.println("\n🔧 Starting WiFi Configuration Portal...");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("📱 Connect to WiFi:");
  Serial.println("   Network: " + String(apSSID));
  Serial.println("   Password: " + String(apPassword));
  Serial.println("🌐 Open browser and go to:");
  Serial.println("   http://192.168.4.1");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID, apPassword);
  
  Serial.println("✅ Access Point Started");
  Serial.println("📡 IP: " + WiFi.softAPIP().toString());
  
  // Setup DNS server for captive portal
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  // Setup web server
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound(handleRoot);
  server.begin();
  
  Serial.println("✅ Web Server Started");
  Serial.println("⏳ Waiting for configuration...\n");
}

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    String device_id = server.arg("device_id");
    
    Serial.println("\n💾 Saving WiFi credentials...");
    Serial.println("SSID: " + ssid);
    Serial.println("Device ID: " + device_id);
    
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putString("device_id", device_id);
    
    server.send(200, "text/plain", "Success! Device will restart...");
    
    Serial.println("✅ Credentials saved!");
    Serial.println("🔄 Restarting in 3 seconds...");
    
    delay(3000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

// ==================== API FUNCTIONS ====================

void registerDeviceWithAPI() {
  if (!wifiConnected) return;
  
  Serial.println("\n📡 Registering device with API...");
  
  HTTPClient http;
  String endpoint = String(apiBaseUrl) + "/devices/register";
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");
  
  DynamicJsonDocument doc(512);
  doc["device_id"] = deviceId;
  doc["device_type"] = "vehicle_tracker";
  doc["firmware_version"] = "3.0";
  doc["emergency_contact"] = phoneNumber;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 200 || httpResponseCode == 201) {
    Serial.println("✅ Device registered successfully!");
    
    // Parse response to get token if provided
    String response = http.getString();
    DynamicJsonDocument responseDoc(1024);
    deserializeJson(responseDoc, response);
    
    if (responseDoc.containsKey("token")) {
      apiToken = responseDoc["token"].as<String>();
      Serial.println("🔑 API Token received");
    }
  } else if (httpResponseCode == 409) {
    Serial.println("ℹ️  Device already registered");
  } else {
    Serial.println("❌ Registration failed. Code: " + String(httpResponseCode));
    Serial.println("Response: " + http.getString());
  }
  
  http.end();
}

void sendTrackingDataToAPI() {
  if (!wifiConnected) return;
  
  HTTPClient http;
  String endpoint = String(apiBaseUrl) + "/tracking";
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");
  
  if (apiToken.length() > 0) {
    http.addHeader("Authorization", "Bearer " + apiToken);
  }
  
  DynamicJsonDocument doc(1024);
  doc["device_id"] = deviceId;
  doc["timestamp"] = utcTime.length() > 0 ? utcTime : "N/A";
  
  // GPS Data
  if (gpsFixed && currentLat != 0 && currentLon != 0) {
    JsonObject location = doc.createNestedObject("location");
    location["latitude"] = currentLat;
    location["longitude"] = currentLon;
    location["altitude"] = altitude;
    location["speed"] = currentSpeed;
    location["satellites"] = satellitesInUse;
    location["fix_quality"] = fixQuality;
  }
  
  // Sensor Data
  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["acceleration_x"] = accelX;
  sensors["acceleration_y"] = accelY;
  sensors["acceleration_z"] = accelZ;
  sensors["total_acceleration"] = totalAccel;
  sensors["gyro_x"] = gyroX;
  sensors["gyro_y"] = gyroY;
  sensors["gyro_z"] = gyroZ;
  sensors["total_gyro"] = totalGyro;
  sensors["vibration"] = piezoValue;
  
  // System Status
  JsonObject status = doc.createNestedObject("status");
  status["gsm_connected"] = gsmReady;
  status["gps_connected"] = gpsFixed;
  status["wifi_signal"] = WiFi.RSSI();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 200 || httpResponseCode == 201) {
    Serial.println("✅ Tracking data synced to API");
  } else if (httpResponseCode > 0) {
    Serial.println("❌ API sync failed. Code: " + String(httpResponseCode));
    Serial.println("Response: " + http.getString());
  } else {
    Serial.println("❌ Connection to API failed");
  }
  
  http.end();
}

void sendAccidentToAPI(String accidentType) {
  if (!wifiConnected) return;
  
  Serial.println("\n🚨 Sending accident data to API...");
  
  HTTPClient http;
  String endpoint = String(apiBaseUrl) + "/accidents";
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");
  
  if (apiToken.length() > 0) {
    http.addHeader("Authorization", "Bearer " + apiToken);
  }
  
  DynamicJsonDocument doc(1536);
  doc["device_id"] = deviceId;
  doc["accident_type"] = accidentType;
  doc["severity"] = "HIGH";
  doc["timestamp"] = utcTime.length() > 0 ? utcTime : "N/A";
  
  // Location data
  if (gpsFixed) {
    JsonObject location = doc.createNestedObject("location");
    location["latitude"] = currentLat;
    location["longitude"] = currentLon;
    location["altitude"] = altitude;
    location["speed_at_impact"] = currentSpeed;
  }
  
  // Sensor readings at impact
  JsonObject impact = doc.createNestedObject("impact_data");
  impact["max_acceleration"] = totalAccel;
  impact["max_rotation"] = totalGyro;
  impact["vibration_level"] = piezoValue;
  
  if (accidentType == "SPEED_DROP") {
    impact["speed_before"] = previousSpeed;
    impact["speed_after"] = currentSpeed;
    impact["speed_drop"] = previousSpeed - currentSpeed;
  }
  
  // Emergency info
  JsonObject emergency = doc.createNestedObject("emergency");
  emergency["contact"] = phoneNumber;
  emergency["sms_sent"] = smsSent;
  emergency["needs_assistance"] = true;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 200 || httpResponseCode == 201) {
    Serial.println("✅ Accident logged to API successfully!");
  } else {
    Serial.println("❌ Failed to log accident. Code: " + String(httpResponseCode));
    Serial.println("Response: " + http.getString());
  }
  
  http.end();
}

void sendOverspeedEventToAPI(unsigned long duration) {
  if (!wifiConnected) return;
  
  HTTPClient http;
  String endpoint = String(apiBaseUrl) + "/overspeed";
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");
  
  if (apiToken.length() > 0) {
    http.addHeader("Authorization", "Bearer " + apiToken);
  }
  
  DynamicJsonDocument doc(768);
  doc["device_id"] = deviceId;
  doc["max_speed"] = currentSpeed;
  doc["duration_seconds"] = (int)duration;
  doc["timestamp"] = utcTime.length() > 0 ? utcTime : "N/A";
  
  if (gpsFixed) {
    JsonObject location = doc.createNestedObject("location");
    location["latitude"] = currentLat;
    location["longitude"] = currentLon;
  }
  
  doc["warning_sent"] = true;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 200 || httpResponseCode == 201) {
    Serial.println("✅ Overspeed event logged to API");
  } else {
    Serial.println("❌ Failed to log overspeed. Code: " + String(httpResponseCode));
  }
  
  http.end();
}

void updateSMSStatusInAPI() {
  if (!wifiConnected) return;
  
  HTTPClient http;
  String endpoint = String(apiBaseUrl) + "/accidents/sms-status";
  http.begin(endpoint);
  http.addHeader("Content-Type", "application/json");
  
  if (apiToken.length() > 0) {
    http.addHeader("Authorization", "Bearer " + apiToken);
  }
  
  DynamicJsonDocument doc(256);
  doc["device_id"] = deviceId;
  doc["sms_sent"] = true;
  doc["timestamp"] = utcTime.length() > 0 ? utcTime : "N/A";
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.PUT(jsonString);
  
  if (httpResponseCode == 200) {
    Serial.println("✅ SMS status updated in API");
  }
  
  http.end();
}

// ==================== END API FUNCTIONS ====================

void handleOverspeedWarning() {
  if (gpsFixed && currentSpeed > OVERSPEED_THRESHOLD) {
    if (!isOverspeeding) {
      isOverspeeding = true;
      overspeedStartTime = millis();
      Serial.println("⚠️  OVERSPEED DETECTED! Starting timer...");
    }
    
    if (millis() - lastBuzzerTime >= 500) {
      buzzerState = !buzzerState;
      digitalWrite(buzzerPin, buzzerState);
      lastBuzzerTime = millis();
      
      static unsigned long lastOverspeedWarning = 0;
      if (millis() - lastOverspeedWarning > 2000) {
        Serial.println("⚠️  OVERSPEED! Speed: " + String(currentSpeed, 1) + " km/h");
        lastOverspeedWarning = millis();
      }
    }
  } else {
    if (isOverspeeding) {
      unsigned long duration = (millis() - overspeedStartTime) / 1000;
      Serial.println("✅ Speed normalized. Duration: " + String(duration) + "s");
      
      if (wifiConnected && duration > 5) {
        sendOverspeedEventToAPI(duration);
      }
      
      isOverspeeding = false;
    }
    
    digitalWrite(buzzerPin, LOW);
    buzzerState = false;
  }
}

void performAccidentDetection() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  
  accelX = a.acceleration.x;
  accelY = a.acceleration.y;
  accelZ = a.acceleration.z;
  gyroX = g.gyro.x;
  gyroY = g.gyro.y;
  gyroZ = g.gyro.z;
  
  totalAccel = sqrt(accelX*accelX + accelY*accelY + accelZ*accelZ);
  totalGyro = sqrt(gyroX*gyroX + gyroY*gyroY + gyroZ*gyroZ);
  piezoValue = analogRead(piezoPin);
  
  bool speedAccident = false;
  if (millis() - lastSpeedCheck > 1000) {
    speedAccident = checkSpeedAccident();
    lastSpeedCheck = millis();
  }
  
  if (isBlinking) {
    if (millis() - blinkStartTime >= 3000) {
      isBlinking = false;
      digitalWrite(dangerLED, LOW);
      if (!accidentDetected) {
        digitalWrite(safeLED, HIGH);
      }
    } else {
      if (millis() - lastBlinkTime >= 200) {
        ledState = !ledState;
        digitalWrite(dangerLED, ledState);
        digitalWrite(safeLED, LOW);
        lastBlinkTime = millis();
      }
    }
  }
  
  bool impactAccident = (piezoValue > PIEZO_THRESHOLD && 
                        (totalAccel > ACCEL_THRESHOLD || totalGyro > GYRO_THRESHOLD));
  bool combinedAccident = speedAccident && piezoValue > PIEZO_THRESHOLD;
  
  if ((impactAccident || combinedAccident) && !accidentDetected) {
    accidentDetected = true;
    accidentTime = millis();
    
    if (!isBlinking) {
      isBlinking = true;
      blinkStartTime = millis();
      lastBlinkTime = millis();
      ledState = true;
    }
    
    String accidentType = impactAccident ? "IMPACT" : "SPEED_DROP";
    
    Serial.println("\n🚨🚨🚨 ACCIDENT DETECTED! 🚨🚨🚨");
    Serial.println("Time: " + String(millis()/1000) + "s");
    Serial.println("Type: " + accidentType);
    Serial.println("Acceleration: " + String(totalAccel) + " g");
    Serial.println("Rotation: " + String(totalGyro) + " rad/s");
    Serial.println("Vibration: " + String(piezoValue));
    Serial.println("🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨");
    
    if (wifiConnected) {
      sendAccidentToAPI(accidentType);
    }
    
    sendEmergencySMS();
    
  } else if (!isBlinking && !accidentDetected) {
    digitalWrite(safeLED, HIGH);
    digitalWrite(dangerLED, LOW);
    
    static unsigned long lastStatusPrint = 0;
    if (millis() - lastStatusPrint > 2000) {
      Serial.print("✅ SAFE - Accel: " + String(totalAccel, 1));
      Serial.print("g | Gyro: " + String(totalGyro, 1));
      Serial.print("rad/s | Shock: " + String(piezoValue));
      Serial.println(" | Speed: " + String(currentSpeed, 1) + "km/h");
      lastStatusPrint = millis();
    }
  }
  
  if (accidentDetected && (millis() - accidentTime > 30000)) {
    Serial.println("🟢 System reset - monitoring resumed");
    accidentDetected = false;
    smsSent = false;
    digitalWrite(safeLED, HIGH);
  }
}

void updateStatusLEDs() {
  unsigned long gpsInterval = gpsFixed ? 500 : 1000;
  if (millis() - lastGpsBlinkTime >= gpsInterval) {
    gpsLedState = !gpsLedState;
    digitalWrite(gpsLED, gpsLedState);
    lastGpsBlinkTime = millis();
  }
  
  unsigned long gsmInterval = gsmReady ? 300 : 1500;
  if (millis() - lastGsmBlinkTime >= gsmInterval) {
    gsmLedState = !gsmLedState;
    digitalWrite(gsmLED, gsmLedState);
    lastGsmBlinkTime = millis();
  }
  
  unsigned long wifiInterval = wifiConnected ? 1000 : 2000;
  if (millis() - lastWifiBlinkTime >= wifiInterval) {
    wifiLedState = !wifiLedState;
    digitalWrite(wifiLED, wifiLedState);
    lastWifiBlinkTime = millis();
  }
}

bool checkSpeedAccident() {
  if (!gpsFixed) return false;
  
  if (previousSpeed > 40.0 && currentSpeed < LOW_SPEED_THRESHOLD) {
    float speedDrop = previousSpeed - currentSpeed;
    if (speedDrop >= SPEED_DROP_THRESHOLD) {
      Serial.println("⚠️  DANGEROUS SPEED DROP!");
      Serial.println("Speed: " + String(previousSpeed) + " -> " + String(currentSpeed) + " km/h");
      previousSpeed = currentSpeed;
      return true;
    }
  }
  
  previousSpeed = currentSpeed;
  return false;
}

void testGSM() {
  Serial.println("\n📱 Testing GSM...");
  
  gsmSerial.println("AT+CSQ");
  delay(2000);
  String signalResp = "";
  unsigned long start = millis();
  while (millis() - start < 3000) {
    if (gsmSerial.available()) {
      signalResp += char(gsmSerial.read());
    }
  }
  
  gsmSerial.println("AT+CREG?");
  delay(2000);  
  String networkResp = "";
  start = millis();
  while (millis() - start < 3000) {
    if (gsmSerial.available()) {
      networkResp += char(gsmSerial.read());
    }
  }
  
  if (signalResp.indexOf("+CSQ:") >= 0 && networkResp.indexOf("+CREG: 0,1") >= 0) {
    Serial.println("✅ GSM ready!");
    gsmReady = true;
  } else {
    Serial.println("⚠️  GSM connection issues");
    gsmReady = false;
  }
}

String getGSMResponse() {
  String response = "";
  unsigned long start = millis();
  
  while (millis() - start < 3000) {
    if (gsmSerial.available()) {
      response += char(gsmSerial.read());
    }
    if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0 || 
        response.indexOf("+CMGS:") >= 0) {
      break;
    }
  }
  
  return response;
}

void sendEmergencySMS() {
  if (smsSent) return;
  
  Serial.println("\n📱 === SENDING EMERGENCY SMS ===");
  
  gsmSerial.println("AT+CMGF=1");
  delay(1000);
  getGSMResponse();
  
  gsmSerial.println("AT+CMGS=\"" + phoneNumber + "\"");
  delay(2000);
  getGSMResponse();
  
  String message = "🚨 VEHICLE ACCIDENT DETECTED! 🚨\n";
  message += "Time: " + utcTime + " UTC\n";
  message += "Device: " + deviceId + "\n";
  
  if (gpsFixed) {
    message += "Location:\n";
    message += "Lat: " + String(currentLat, 6) + "\n";
    message += "Lon: " + String(currentLon, 6) + "\n";
    message += "Speed: " + String(currentSpeed, 1) + " km/h\n";
    message += "Map: https://maps.google.com/?q=" + String(currentLat, 6) + "," + String(currentLon, 6);
  } else {
    message += "GPS: No location available\n";
    message += "Please call immediately!";
  }
  
  message += "\n⚠️ EMERGENCY ASSISTANCE NEEDED ⚠️";
  
  gsmSerial.print(message);
  delay(1000);
  gsmSerial.write(26);
  
  delay(8000);
  String resp = getGSMResponse();
  
  if (resp.indexOf("+CMGS:") >= 0) {
    Serial.println("✅ EMERGENCY SMS SENT!");
    smsSent = true;
    
    if (wifiConnected && accidentDetected) {
      updateSMSStatusInAPI();
    }
  } else {
    Serial.println("❌ SMS failed - retrying...");
  }
  
  Serial.println("===============================\n");
}

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
  if (sentence.startsWith("$GPGGA")) {
    parseGPGGA(sentence);
  } else if (sentence.startsWith("$GPRMC")) {
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
  
  satellitesInUse = parts[7].toInt();
  
  if (gpsFixed && parts[2].length() > 0 && parts[4].length() > 0) {
    currentLat = convertDMtoDecimal(parts[2], parts[3]);
    currentLon = convertDMtoDecimal(parts[4], parts[5]);
    altitude = parts[9].toFloat();
    
    static unsigned long lastGpsShow = 0;
    if (millis() - lastGpsShow > 10000) {
      Serial.println("🛰️  GPS: " + fixQuality + " | Sats: " + String(satellitesInUse) + 
                    " | Speed: " + String(currentSpeed, 1) + "km/h");
      lastGpsShow = millis();
    }
  }
}

void parseGPRMC(String sentence) {
  String parts[15];
  int partCount = splitString(sentence, ',', parts, 15);
  
  if (partCount < 8 || parts[7].length() == 0) return;
  
  float speedKnots = parts[7].toFloat();
  currentSpeed = speedKnots * 1.852;
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