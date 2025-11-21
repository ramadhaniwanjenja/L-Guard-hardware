# 🛡️ L-Guard Vehicle Safety System

**Production Version 3.0** - Cloud-Connected Accident Detection & Vehicle Tracking System

---

## 📋 Overview

L-Guard is an advanced IoT-based vehicle safety system that provides real-time accident detection, GPS tracking, overspeed warnings, and emergency notifications. The system combines multiple sensors with cloud connectivity to ensure comprehensive vehicle monitoring and rapid emergency response.

### Key Features
- ✅ **Real-time Accident Detection** (Impact & Sudden Deceleration)
- 🛰️ **GPS Location Tracking** with live coordinates
- 📱 **Automatic Emergency SMS** with location data
- 🔊 **Overspeed Warning System** (>40 km/h threshold)
- ☁️ **Cloud Synchronization** via Supabase
- 📡 **WiFi Configuration Portal** for easy setup
- 💾 **Persistent Storage** for credentials
- 📊 **Multi-sensor Data Logging**

---

## 🔌 Hardware Components

### Required Components
| Component | Pin | Description |
|-----------|-----|-------------|
| **MPU6050** | SDA: 21, SCL: 22 | 6-axis accelerometer & gyroscope |
| **GPS Module** | RX: 19, TX: 18 | NEO-6M or similar UART GPS |
| **GSM Module** | RX: 16, TX: 17 | SIM800L or SIM900 for SMS |
| **Piezo Sensor** | Pin 34 (ADC) | Vibration/impact detection |
| **Safe LED** | Pin 2 | Green status indicator |
| **Danger LED** | Pin 4 | Red alert indicator |
| **GPS LED** | Pin 5 | GPS connection status |
| **GSM LED** | Pin 25 | GSM network status |
| **WiFi LED** | Pin 27 | WiFi connection status |
| **Buzzer** | Pin 26 | Overspeed audio warning |
| **Config Button** | Pin 0 (BOOT) | WiFi reset button |

### Wiring Diagram Notes
- **MPU6050**: I2C connection (3.3V logic)
- **GPS Module**: Hardware Serial1 (9600 baud)
- **GSM Module**: Hardware Serial2 (9600 baud)
- **Piezo Sensor**: Analog input with pull-down resistor recommended
- **All LEDs**: Use 220Ω current-limiting resistors

---

## 🚀 Getting Started

### Prerequisites
```cpp
// Required Arduino Libraries:
- Adafruit_MPU6050
- Adafruit_Sensor
- WiFi (ESP32)
- WebServer (ESP32)
- Preferences (ESP32)
- DNSServer
- HTTPClient
- ArduinoJson
```

### Installation Steps

1. **Install Arduino IDE** with ESP32 board support
2. **Install Required Libraries** via Library Manager
3. **Configure Supabase** (see Cloud Setup section)
4. **Upload Code** to ESP32 board
5. **First-Time Setup** via WiFi portal

---

## 📡 WiFi Configuration

### Initial Setup (First Boot)

1. **Power on the device** - LEDs will perform boot animation
2. **Connect to WiFi**:
   - Network: `L-Guard-Setup`
   - Password: `12345678`
3. **Open Browser** and navigate to: `http://192.168.4.1`
4. **Enter Credentials**:
   - Your WiFi SSID
   - WiFi Password
   - Device ID (default: VEHICLE_001)
5. **Save** - Device will restart and connect automatically

### Resetting WiFi Credentials

**Hold the BOOT button for 3 seconds** during startup to clear saved credentials and enter configuration mode again.

---

## 💡 LED Status Indicators

### LED Behavior Guide

#### **Safe LED (Green - Pin 2)**
- **Solid ON** → System operating normally, no threats detected
- **OFF** → Accident detected or danger state

#### **Danger LED (Red - Pin 4)**
- **Blinking (200ms)** → Accident detected! (blinks for 3 seconds)
- **Solid ON (startup)** → WiFi credentials reset confirmation
- **OFF** → Normal operation

#### **GPS LED (Yellow - Pin 5)**
- **Fast Blink (500ms)** → GPS fix acquired, valid location data
- **Slow Blink (1000ms)** → Searching for satellites, no fix
- **OFF** → GPS module not responding

#### **GSM LED (Blue - Pin 25)**
- **Fast Blink (300ms)** → GSM connected to network, ready
- **Slow Blink (1500ms)** → No network connection, searching
- **OFF** → GSM module not responding

#### **WiFi LED (White - Pin 27)**
- **Solid ON** → Connected to WiFi, cloud sync active
- **Fast Blink (200ms)** → Configuration portal active
- **Slow Blink (2000ms)** → WiFi connection failed, no cloud sync
- **OFF** → WiFi disabled or disconnected

#### **Buzzer (Pin 26)**
- **Beeping (500ms interval)** → OVERSPEED WARNING! Speed >40 km/h
- **Silent** → Speed within safe limits

---

## 🚨 Accident Detection System

### Detection Methods

#### **1. Impact Detection**
Triggers when **ANY** of these conditions are met:
```
Piezo Sensor > 1200 (vibration threshold)
AND
(Total Acceleration > 15g OR Total Rotation > 3 rad/s)
```

#### **2. Sudden Deceleration Detection**
Triggers when:
```
Previous Speed > 40 km/h
AND
Current Speed < 10 km/h (within 1 second)
AND
Speed Drop ≥ 30 km/h
AND
Piezo Sensor > 1200
```

### Accident Response Sequence

1. **Immediate Actions** (within milliseconds):
   - ✅ Danger LED starts blinking (3 seconds)
   - ✅ Safe LED turns OFF
   - ✅ Accident data logged to Serial Monitor

2. **Cloud Logging** (if WiFi connected):
   - ✅ Accident event sent to Supabase
   - ✅ Includes: type, severity, location, sensor readings, timestamp

3. **Emergency SMS** (if GSM ready):
   - ✅ Sends SMS to emergency contact: `+250795613644`
   - ✅ Message includes:
     - Accident alert
     - GPS coordinates
     - Google Maps link
     - Device ID
     - UTC timestamp
     - Current speed

4. **System Recovery**:
   - After 30 seconds, system automatically resets to monitoring mode

---

## ⚠️ Overspeed Warning System

### Configuration
```cpp
const float OVERSPEED_THRESHOLD = 40.0;  // km/h
```

### Behavior
- **Speed > 40 km/h**: Buzzer beeps every 500ms
- **Duration Tracking**: Counts how long overspeed condition persists
- **Cloud Logging**: If duration > 5 seconds, event logged to Supabase
- **Auto-Reset**: Buzzer stops when speed drops below threshold

### Console Output
```
⚠️  OVERSPEED! Speed: 45.2 km/h
✅ Speed normalized. Duration: 12s
✅ Overspeed event logged
```

---

## 🛰️ GPS Tracking

### Supported NMEA Sentences
- **$GPGGA**: Position, altitude, fix quality, satellites
- **$GPRMC**: Speed, date, time, coordinates

### GPS Data Parsed
- **Latitude/Longitude**: Decimal degrees (converted from DM format)
- **Altitude**: Meters above sea level
- **Speed**: km/h (converted from knots)
- **Satellites**: Number of satellites in use
- **Fix Quality**: No Fix / GPS Fix / DGPS Fix
- **UTC Time**: HH:MM:SS format

### GPS Status Messages
```
🛰️  GPS: GPS Fix | Sats: 8 | Speed: 35.2km/h
```

---

## ☁️ Cloud Integration (Supabase)

### Database Tables

#### **1. vehicle_tracking** (Real-time telemetry)
Syncs every 30 seconds with:
- Device ID, GPS coordinates, speed, altitude
- Accelerometer readings (X, Y, Z, Total)
- Gyroscope readings (X, Y, Z, Total)
- Piezo vibration level
- Satellite count, fix quality
- GSM/GPS connection status
- UTC timestamp

#### **2. accident_events** (Emergency logs)
Logged immediately when accident detected:
- Device ID, accident type (IMPACT/SPEED_DROP)
- Severity level (HIGH)
- GPS coordinates at impact
- Speed before/after (for speed drops)
- Max acceleration, rotation, vibration
- Emergency contact, SMS status
- UTC timestamp

#### **3. overspeed_events** (Speed violations)
Logged when overspeed duration > 5 seconds:
- Device ID, max speed recorded
- Duration in seconds
- GPS coordinates
- Warning status

#### **4. device_status** (Heartbeat)
Updated with tracking data:
- Current location, speed, altitude
- GSM/GPS status
- Satellite count
- System voltage

### Supabase Configuration

Update these credentials in the code:
```cpp
const char* supabaseUrl = "https://YOUR_PROJECT.supabase.co";
const char* supabaseKey = "YOUR_ANON_KEY";
```

---

## 📱 GSM Communication

### SMS Configuration
```cpp
String phoneNumber = "+250795613644";  // Emergency contact
```

### SMS Format
```
🚨 VEHICLE ACCIDENT DETECTED! 🚨
Time: 14:35:22 UTC
Device: VEHICLE_001
Location:
Lat: -1.935114
Lon: 30.082111
Speed: 12.5 km/h
Map: https://maps.google.com/?q=-1.935114,30.082111
⚠️ EMERGENCY ASSISTANCE NEEDED ⚠️
```

### GSM Testing
System performs automatic GSM test at startup:
- Checks signal strength (`AT+CSQ`)
- Verifies network registration (`AT+CREG?`)
- Sets GSM ready flag if successful

---

## 🔧 System Thresholds

### Configurable Parameters
```cpp
// Accident Detection
const float ACCEL_THRESHOLD = 15.0;        // g-force
const float GYRO_THRESHOLD = 3.0;          // rad/s
const int PIEZO_THRESHOLD = 1200;          // analog value (0-4095)

// Speed Monitoring
const float SPEED_DROP_THRESHOLD = 30.0;   // km/h
const float LOW_SPEED_THRESHOLD = 10.0;    // km/h
const float OVERSPEED_THRESHOLD = 40.0;    // km/h

// Timing
const unsigned long DATA_SEND_INTERVAL = 30000;  // ms (30 seconds)
```

---

## 📊 Serial Monitor Output

### Normal Operation
```
✅ SAFE - Accel: 9.8g | Gyro: 0.1rad/s | Shock: 450 | Speed: 35.2km/h
🛰️  GPS: GPS Fix | Sats: 8 | Speed: 35.2km/h
✅ Data synced to cloud
```

### Accident Detection
```
🚨🚨🚨 ACCIDENT DETECTED! 🚨🚨🚨
Time: 1234s
Type: IMPACT
Acceleration: 18.5 g
Rotation: 4.2 rad/s
Vibration: 2450
🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨
✅ Accident logged to cloud
📱 === SENDING EMERGENCY SMS ===
✅ EMERGENCY SMS SENT!
```

### Overspeed Warning
```
⚠️  OVERSPEED DETECTED! Starting timer...
⚠️  OVERSPEED! Speed: 45.2 km/h
✅ Speed normalized. Duration: 12s
✅ Overspeed event logged
```

---

## 🔐 Security & Privacy

- **WiFi Credentials**: Stored securely in ESP32 NVS (Non-Volatile Storage)
- **Supabase Key**: Anon key with Row-Level Security policies recommended
- **SMS Privacy**: Emergency contact configurable per device
- **Data Encryption**: HTTPS for all cloud communications

---

## 🛠️ Troubleshooting

### GPS Not Getting Fix
- ✅ Ensure clear sky view (outdoor testing)
- ✅ Wait 2-5 minutes for cold start
- ✅ Check GPS LED (should blink slowly)
- ✅ Verify TX/RX pins not swapped

### GSM Not Connecting
- ✅ Check SIM card inserted correctly
- ✅ Verify SIM has credit/active plan
- ✅ GSM antenna properly connected
- ✅ Check power supply (GSM draws high current)

### WiFi Won't Connect
- ✅ Reset credentials (hold BOOT 3 seconds)
- ✅ Check SSID/password spelling
- ✅ Ensure 2.4GHz WiFi (ESP32 doesn't support 5GHz)
- ✅ Move closer to router during setup

### Cloud Sync Failing
- ✅ Verify Supabase URL and API key
- ✅ Check database table schemas match
- ✅ Review Supabase logs for errors
- ✅ Ensure WiFi connected (check WiFi LED)

### False Accident Triggers
- ✅ Adjust `ACCEL_THRESHOLD` (increase if too sensitive)
- ✅ Adjust `PIEZO_THRESHOLD` (increase for less sensitivity)
- ✅ Secure piezo sensor (prevent rattling)
- ✅ Test on smooth road first

---

## 📈 Performance Specifications

- **GPS Update Rate**: ~1 Hz (1 reading per second)
- **Sensor Sampling**: ~10 Hz (100ms loop)
- **Cloud Sync**: Every 30 seconds
- **SMS Send Time**: 8-12 seconds
- **Accident Detection Latency**: <100ms
- **WiFi Reconnect**: Automatic on disconnect

---

## 🔄 System States

### Normal Operation
- Green LED: ON
- Red LED: OFF
- GPS/GSM/WiFi LEDs: Blinking per status
- Buzzer: OFF
- Serial: Status updates every 2s

### Configuration Mode
- WiFi LED: Fast blinking
- Access Point: Active
- Web Server: Running on 192.168.4.1

### Accident State
- Red LED: Blinking rapidly (3 seconds)
- Green LED: OFF
- SMS: Sending
- Cloud: Logging event
- Duration: 30 seconds before auto-reset

### Overspeed State
- Buzzer: Beeping (500ms)
- Normal LEDs: Continue per status
- Cloud: Logging if duration > 5s

---

## 📝 Version History

### Version 3.0 (Current)
- ✅ Cloud integration with Supabase
- ✅ WiFi configuration portal
- ✅ Persistent credential storage
- ✅ Enhanced LED indicators
- ✅ Overspeed event logging

### Version 2.x
- ✅ GPS tracking integration
- ✅ GSM emergency SMS
- ✅ Multi-sensor accident detection

### Version 1.x
- ✅ Basic impact detection
- ✅ MPU6050 integration

---

## 🤝 Support & Contact

**Emergency Contact**: +250795613644  
**Device ID**: Configurable via WiFi portal  
**Default Device ID**: VEHICLE_001

---

## ⚖️ License

This project is for educational and safety purposes. Ensure compliance with local vehicle monitoring regulations.

---

## 🎯 Quick Reference Card

| Event | LED Behavior | Buzzer | SMS | Cloud |
|-------|--------------|--------|-----|-------|
| **Normal** | Green ON | OFF | NO | Sync 30s |
| **Accident** | Red Blink 3s | OFF | YES | Immediate |
| **Overspeed** | Normal | Beep | NO | If >5s |
| **GPS Fix** | Yellow Fast | OFF | NO | - |
| **No GPS** | Yellow Slow | OFF | NO | - |
| **WiFi Config** | White Fast | OFF | NO | NO |
| **GSM Ready** | Blue Fast | OFF | - | - |

---

**🛡️ Stay Safe. Drive Smart. L-Guard Protects.**