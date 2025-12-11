# 🚗 L-GUARD v5.4 - Vehicle Safety System
## Complete Installation & Operation Guide

---

## 📋 TABLE OF CONTENTS
1. [System Overview](#system-overview)
2. [Hardware Requirements](#hardware-requirements)
3. [Pin Connections](#pin-connections)
4. [How It Works](#how-it-works)
5. [Accident Detection Logic](#accident-detection-logic)
6. [Speed Alert System](#speed-alert-system)
7. [Smart Data Sync](#smart-data-sync)
8. [Sleep Mode](#sleep-mode)
9. [LED Indicators](#led-indicators)
10. [Emergency Response](#emergency-response)
11. [Configuration](#configuration)
12. [Troubleshooting](#troubleshooting)

---

## 🎯 SYSTEM OVERVIEW

L-GUARD is an intelligent vehicle safety system that:
- **Detects accidents** using multi-sensor fusion
- **Alerts emergency contacts** via SMS with GPS location
- **Monitors speed** and alerts when exceeding limits
- **Tracks vehicle** in real-time to cloud API
- **Saves battery** with smart sleep mode
- **Reduces data usage** by 80% with intelligent sync

---

## 🔧 HARDWARE REQUIREMENTS

### Required Components:
| Component | Model | Purpose |
|-----------|-------|---------|
| Microcontroller | **ESP32** | Main processor |
| Accelerometer | **ADXL345** | Impact detection (high sensitivity) |
| Gyroscope/Accel | **MPU6050** | Tilt/rollover detection |
| GPS Module | **GP-02 (NEO-6M/7M)** | Location & speed tracking |
| GSM Modem | **SIM A7670E** | SMS & internet connectivity |
| Vibration Sensor | **SW-420** | Impact gate/trigger |
| Buzzer | **Active 5V** | Speed alerts |
| LEDs | **5x LEDs** | Status indicators |
| Buttons | **2x Push buttons** | User controls |
| Battery | **LiPo 3.7V** | Power supply |

### Optional:
- Battery monitor circuit (voltage divider)
- Bike monitor sensor (ignition detection)

---

## 🔌 PIN CONNECTIONS

### 📡 SIM A7670E Modem
```
ESP32 Pin    →    A7670E Pin         Function
-------------------------------------------------
GPIO 5       →    RXD                ESP32 transmit to modem
GPIO 4       →    TXD                ESP32 receive from modem
GPIO 15      →    PWRKEY             Power on/off control
GPIO 18      →    DTR                Sleep control
GPIO 19      →    RESET              Hardware reset
GPIO 2       →    RI                 Ring indicator (optional)
3.3V/5V      →    VCC                Power supply (3.8-4.2V)
GND          →    GND                Ground
```

**⚠️ IMPORTANT:** A7670E needs 2A current capability during transmission!

---

### 🛰️ GP-02 GPS Module
```
ESP32 Pin    →    GPS Pin            Function
-------------------------------------------------
GPIO 16      →    TX                 GPS transmit (to ESP32 RX)
GPIO 17      →    RX                 GPS receive (from ESP32 TX)
5V           →    VCC                Power (5V preferred)
GND          →    GND                Ground
```

**Note:** GPS needs clear sky view. Place antenna near window/outside.

---

### 📊 Sensors (I2C Bus)

#### ADXL345 Accelerometer
```
ESP32 Pin    →    ADXL345 Pin        Function
-------------------------------------------------
GPIO 21      →    SDA                I2C Data
GPIO 22      →    SCL                I2C Clock
3.3V         →    VCC                Power
GND          →    GND                Ground
                  CS → VCC           (I2C mode)
```

#### MPU6050 Gyroscope/Accelerometer
```
ESP32 Pin    →    MPU6050 Pin        Function
-------------------------------------------------
GPIO 21      →    SDA                I2C Data (shared)
GPIO 22      →    SCL                I2C Clock (shared)
3.3V         →    VCC                Power
GND          →    GND                Ground
                  AD0 → GND          (Address 0x69)
```

**⚠️ CRITICAL:** MPU6050 AD0 pin MUST be connected to GND to set address to 0x69!

---

### 🔔 Buzzer & Vibration Sensor
```
ESP32 Pin    →    Component          Function
-------------------------------------------------
GPIO 25      →    Buzzer (+)         Speed alert beeper
GND          →    Buzzer (-)         Ground
GPIO 32      →    SW-420 (D0)        Vibration digital output
3.3V         →    SW-420 (VCC)       Power
GND          →    SW-420 (GND)       Ground
```

**Vibration Sensor Settings:**
- Adjust potentiometer for medium sensitivity
- Should NOT trigger from engine vibration
- SHOULD trigger from impacts/bumps

---

### 🎮 Buttons & Controls
```
ESP32 Pin    →    Button             Function
-------------------------------------------------
GPIO 26      →    OK Button          Cancel accident alert
GPIO 27      →    Menu Button        (Reserved for future)
GND          →    Button (other)     Common ground
```

**Note:** Internal pull-up resistors enabled. Buttons connect to GND when pressed.

---

### 💡 LED Indicators
```
ESP32 Pin    →    LED Color          Status Indication
-------------------------------------------------
GPIO 12      →    Green LED          System SAFE
GPIO 14      →    Red LED            DANGER/Accident detected
GPIO 13      →    Blue LED           GPS status
GPIO 23      →    Yellow LED         GSM/Modem status
Optional     →    White LED          System power (always on)
```

**LED Patterns:**
- **Solid Green:** System safe, no issues
- **Fast Blink Red (150ms):** Accident detected, waiting for cancel
- **Solid Red:** SMS sent, emergency response active
- **Fast Blink Blue (250ms):** GPS has fix
- **Slow Blink Blue (1000ms):** GPS no fix, searching
- **Fast Blink Yellow (300ms):** Modem connected
- **Slow Blink Yellow (1500ms):** Modem not ready

---

### 🔋 Battery Monitor (Optional)
```
ESP32 Pin    →    Circuit            Function
-------------------------------------------------
GPIO 34      →    Voltage Divider    Battery voltage monitor
                  
Circuit:
Battery (+) ──┬── 10kΩ ──┬── GPIO 34
              │          │
              │         10kΩ
              │          │
             GND        GND

This creates 2:1 divider for 3.3-4.2V battery → 1.65-2.1V at GPIO 34
```

---

### 🏍️ Vehicle Monitor (Optional)
```
ESP32 Pin    →    Connection         Function
-------------------------------------------------
GPIO 33      →    Ignition signal    Detects vehicle on/off
```

---

## ⚙️ HOW IT WORKS

### System Flow
```
┌─────────────────────────────────────────────────┐
│              POWER ON / RESET                   │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│  Initialize Hardware (10 seconds)               │
│  • ESP32 pins                                   │
│  • ADXL345 accelerometer                        │
│  • MPU6050 gyroscope                            │
│  • GPS module                                   │
│  • SIM A7670E modem                             │
│  • Connect to cellular network                  │
│  • Establish GPRS connection                    │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│  GPS Warm-up Period (10 seconds)                │
│  • Allow GPS to acquire satellites              │
│  • Speed alerts DISABLED during warm-up         │
│  • Prevents false alarms on startup             │
└────────────────┬────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────┐
│          MAIN OPERATION LOOP                    │
│                                                 │
│  ┌─────────────────────────────────────┐       │
│  │  Every 50ms: Read Impact Sensors    │       │
│  │  • ADXL345: High-precision impact   │       │
│  │  • MPU6050: Tilt/rollover angles    │       │
│  │  • SW-420: Vibration gate trigger   │       │
│  │  • Check accident conditions         │       │
│  └─────────────────────────────────────┘       │
│                                                 │
│  ┌─────────────────────────────────────┐       │
│  │  Every 1 second: Read GPS           │       │
│  │  • Location (lat/lon)                │       │
│  │  • Speed (km/h)                      │       │
│  │  • Heading (degrees)                 │       │
│  │  • Satellites count                  │       │
│  │  • HDOP (accuracy)                   │       │
│  │  • Check speed limit                 │       │
│  └─────────────────────────────────────┘       │
│                                                 │
│  ┌─────────────────────────────────────┐       │
│  │  Every 30 seconds: Check Changes    │       │
│  │  • Has location changed? (11m+)      │       │
│  │  • Has speed changed? (10km/h+)      │       │
│  │  • Has acceleration changed? (2g+)   │       │
│  │  • Has tilt changed? (30°+)          │       │
│  │  • Did vehicle start/stop moving?    │       │
│  │  → If YES: Send to API               │       │
│  │  → If NO: Skip (save data!)          │       │
│  └─────────────────────────────────────┘       │
│                                                 │
│  ┌─────────────────────────────────────┐       │
│  │  Every 5 minutes: Force Heartbeat   │       │
│  │  • Send data even if no changes      │       │
│  │  • Keeps device registered online    │       │
│  │  • Prevents timeout/disconnection    │       │
│  └─────────────────────────────────────┘       │
│                                                 │
│  ┌─────────────────────────────────────┐       │
│  │  After 2 minutes idle: SLEEP MODE   │       │
│  │  • No movement/speed/tilt changes    │       │
│  │  • Enter light sleep for 10s         │       │
│  │  • Wake up, check sensors            │       │
│  │  • Repeat until activity detected    │       │
│  │  • Saves ~66% battery power          │       │
│  └─────────────────────────────────────┘       │
└─────────────────────────────────────────────────┘
```

---

## 🚨 ACCIDENT DETECTION LOGIC

### Detection Algorithm (Multi-Sensor Fusion)

The system uses **5 different sensors** and **6 detection scenarios**:

#### 1️⃣ **SEVERE_IMPACT** (Highest Priority)
```
Conditions:
✅ ADXL345 Total Force > 15g              (High impact threshold)
✅ Vibration Sensor = TRIGGERED           (Physical shock detected)

Result: IMMEDIATE ALERT
Type: "SEVERE_IMPACT"
Severity: HIGH
Example: Head-on collision, hitting wall at speed
```

#### 2️⃣ **ROLLOVER**
```
Conditions:
✅ MPU6050 Roll Angle > 70° OR Pitch > 70°  (Vehicle on side/roof)
✅ Sustained for 3+ readings (150ms+)        (Not just a bump)
✅ Vibration Sensor = TRIGGERED              (Physical impact)

Result: IMMEDIATE ALERT
Type: "ROLLOVER"
Severity: HIGH
Example: Vehicle flipped over, rolled down embankment
```

#### 3️⃣ **SPEED_DROP_COLLISION**
```
Conditions:
✅ Previous Speed > 40 km/h                (Was moving at highway speed)
✅ Current Speed < 10 km/h                 (Suddenly almost stopped)
✅ Speed Drop ≥ 30 km/h                    (Rapid deceleration)
✅ ADXL345 Total Force > 8g                (Medium-high impact)
✅ Vibration Sensor = TRIGGERED            (Physical shock)
✅ GPS Fix = VALID                         (Reliable speed data)

Result: IMMEDIATE ALERT
Type: "SPEED_DROP_COLLISION"
Severity: HIGH
Example: Rear-ended at highway speed, frontal collision
```

#### 4️⃣ **IMPACT_COLLISION**
```
Conditions:
✅ ADXL345 Total Force > 8g                (Medium impact threshold)
✅ Vibration Sensor = TRIGGERED            (Physical shock)

Result: IMMEDIATE ALERT
Type: "IMPACT_COLLISION"
Severity: MEDIUM
Example: T-bone collision, hit parked car
```

#### 5️⃣ **VEHICLE_FLIP**
```
Conditions:
✅ MPU6050 Roll Angle > 45°                (Significant tilt)
✅ MPU6050 Gyro Total > 3.0 rad/s          (Rapid rotation)
✅ Vibration Sensor = TRIGGERED            (Physical impact)

Result: IMMEDIATE ALERT
Type: "VEHICLE_FLIP"
Severity: MEDIUM
Example: Vehicle tipping over, spinning out
```

#### 6️⃣ **LOW_IMPACT** (Monitoring Only)
```
Conditions:
✅ ADXL345 Total Force > 3g                (Low impact threshold)

Result: NO ALERT (logged only)
Note: Recorded but not severe enough for emergency
Example: Speed bump at high speed, pothole hit
```

---

### 🔍 **Why Vibration Sensor is Critical**

The **SW-420 vibration sensor** acts as a **GATE**:
- **Prevents false positives** from sensor noise
- **Confirms physical impact** actually occurred
- **Required for ALL accident types** (except monitoring)

**Without vibration trigger:**
- Dropping phone → Would trigger high G-force
- Slamming door → Would register on accelerometer
- Hard braking → Would show deceleration

**With vibration gate:**
- Only REAL physical impacts trigger alerts
- System ignores electronic noise
- Much higher reliability

---

### ⏱️ **Accident Alert Timeline**

```
ACCIDENT DETECTED
       ↓
┌──────────────────────────────────────┐
│  T = 0 seconds                       │
│  • Buzzer: SILENT (no beeping)       │
│  • Red LED: Fast blinking            │
│  • Serial: Display accident details  │
│  • Message: "Press CANCEL within 10s"│
└──────────────────────────────────────┘
       ↓
┌──────────────────────────────────────┐
│  T = 0-10 seconds (Cancel Window)    │
│  • Monitor OK button                 │
│  • If pressed → Cancel everything    │
│  • If not pressed → Continue to send │
└──────────────────────────────────────┘
       ↓
┌──────────────────────────────────────┐
│  T = 10 seconds (Timer Expires)      │
│  • No cancellation received          │
│  • Proceed with emergency response   │
└──────────────────────────────────────┘
       ↓
┌──────────────────────────────────────┐
│  Emergency SMS Sending                │
│  • Format emergency message          │
│  • Include: Type, Impact, Location   │
│  • Send to: +2507929577181           │
│  • Include Google Maps link          │
└──────────────────────────────────────┘
       ↓
┌──────────────────────────────────────┐
│  API Data Upload (if GPRS connected) │
│  • Build telemetry JSON              │
│  • POST to backend API               │
│  • Log accident in database          │
└──────────────────────────────────────┘
       ↓
┌──────────────────────────────────────┐
│  Alert Complete                      │
│  • Red LED: Solid ON                 │
│  • System: Wait for manual reset     │
│  • Continue monitoring sensors       │
└──────────────────────────────────────┘
```

---

### 📱 **Emergency SMS Format**

When accident is detected and 10 seconds expire, this SMS is sent:

```
!! VEHICLE ACCIDENT !!

Device: VEHICLE_001
Type: SEVERE_IMPACT
Impact: 18.45g
Time: 14:23:45

LOCATION:
Lat: -1.956789
Lon: 30.123456
Speed: 0.0 km/h
http://maps.google.com/maps?q=-1.956789,30.123456
```

If GPS not available:
```
!! VEHICLE ACCIDENT !!

Device: VEHICLE_001
Type: ROLLOVER
Impact: 12.30g
Time: 14:23:45

GPS: No location
Call immediately!
```

---

## 🏎️ SPEED ALERT SYSTEM

### How Speed Detection Works

The system uses **GPS speed data** with **strict validation**:

#### ✅ **Speed Alert Conditions (ALL must be TRUE):**

```
1. GPS Warm-up Complete
   ⏰ System running > 10 seconds
   → Prevents false alarms on startup

2. GPS Fix Valid
   📡 GPS has valid position lock
   → Not searching for satellites

3. Sufficient Satellites
   🛰️ Satellites ≥ 5
   → Minimum for accurate speed
   → More satellites = better accuracy

4. Good GPS Accuracy (HDOP)
   📊 HDOP < 2.5
   → Horizontal Dilution of Precision
   → Lower HDOP = better accuracy
   → 2.5 is "Good" accuracy threshold

5. Valid Speed Reading
   🏎️ Speed > 5 km/h
   → Eliminates GPS noise/jitter
   → Ignores stationary drift

6. Over Speed Limit
   ⚠️ Speed > 60 km/h
   → Configurable threshold
   → Default set to 60 km/h
```

#### 📊 **HDOP Accuracy Scale:**
```
HDOP Value    Quality      Description
-------------------------------------------------
< 1.0         Ideal        Military-grade precision
1.0 - 2.0     Excellent    Perfect for speed detection
2.0 - 5.0     Good         Acceptable for navigation
5.0 - 10.0    Moderate     Position approximate
> 10.0        Poor         Unreliable, don't use
```

---

### 🔔 **Speed Alert Behavior**

```
WHEN ALL CONDITIONS MET:
┌────────────────────────────────────────┐
│  Speed > 60 km/h Detected              │
│  • Buzzer: BEEP (200ms ON)             │
│  • Delay: 800ms OFF                    │
│  • Repeat: Every 1 second              │
│  • Serial: Display warning             │
│  • Continue: Until speed drops         │
└────────────────────────────────────────┘

SERIAL OUTPUT:
⚠️ WARNING: OVERSPEEDING!
Speed: 75.3 km/h
Limit: 60 km/h
Sats: 7 | HDOP: 1.2
GPS Quality: EXCELLENT


WHEN ANY CONDITION FAILS:
┌────────────────────────────────────────┐
│  Condition Not Met                     │
│  • Buzzer: OFF (silent)                │
│  • No beeping                          │
│  • Wait for conditions                 │
└────────────────────────────────────────┘

DEBUG OUTPUT (every 10 seconds):
⚠️ Speed check: Not enough satellites (4/5)
⚠️ Speed check: Poor GPS accuracy (HDOP: 3.2)
⚠️ Speed check: No GPS fix
```

---

### 🚫 **Why Speed Alerts WON'T Trigger:**

| Scenario | Reason | Solution |
|----------|--------|----------|
| Startup beeping | GPS not warmed up | Wait 10 seconds |
| Indoor beeping | No GPS fix | Move to open area |
| Tunnel beeping | Lost satellite lock | Wait for GPS reacquisition |
| Cloudy day beeping | High HDOP (poor accuracy) | Wait for better signal |
| Stationary beeping | GPS drift below 5 km/h | Already filtered out |
| False speed reading | Only 3-4 satellites | Need 5+ satellites |

---

## 📡 SMART DATA SYNC

### How Data Saving Works

Instead of sending data every 30 seconds (wasteful), the system **only sends when something changes**:

#### 📊 **Change Detection Thresholds:**

```
Data Type          Threshold       Example
-------------------------------------------------
Location           11 meters       Vehicle moved to new location
Speed              10 km/h         Acceleration or deceleration
Acceleration       2g force        Bump, hard braking
Tilt Angle         30 degrees      Vehicle tilted (hill, turn)
Battery            10%             Battery drained/charged
Vehicle State      Start/Stop      Engine turned on/off
```

#### 🔄 **Sync Decision Flow:**

```
Every 30 seconds:
┌─────────────────────────────────────┐
│  Check for Changes                  │
├─────────────────────────────────────┤
│  Has location changed > 11m?        │
│  Has speed changed > 10 km/h?       │
│  Has acceleration changed > 2g?     │
│  Has tilt changed > 30°?            │
│  Has battery changed > 10%?         │
│  Did vehicle start moving?          │
│  Did vehicle stop moving?           │
└─────────────────┬───────────────────┘
                  │
        ┌─────────┴─────────┐
        │                   │
       YES                 NO
        │                   │
        ▼                   ▼
┌───────────────┐   ┌──────────────────┐
│  SEND DATA    │   │  SKIP SENDING    │
│  to API       │   │  Save bandwidth  │
│  Update prev  │   │  Save battery    │
│  values       │   │  Save money      │
└───────────────┘   └──────────────────┘
```

#### ⏰ **5-Minute Heartbeat:**

Even if **nothing changes**, data is sent every **5 minutes**:
```
Purpose:
• Keep device online
• Prevent server timeout
• Confirm system is alive
• Update last-seen timestamp

This ensures:
→ Server knows device is working
→ Connection stays active
→ Emergency alerts can be received
```

---

### 💾 **Data Usage Comparison:**

```
OLD SYSTEM (Send Every 30s):
• 2 sends/minute × 60 minutes = 120 sends/hour
• 120 sends × 24 hours = 2,880 sends/day
• ~500 bytes/send = 1.44 MB/day
• Cost: HIGH (cellular data charges)

NEW SYSTEM (Smart Sync):
• Average 10-15 sends/hour (city driving)
• Average 240-360 sends/day
• ~500 bytes/send = 0.18 MB/day
• Cost: LOW (87% reduction!)

PARKED VEHICLE:
• Only heartbeat: 12 sends/hour
• 288 sends/day
• 0.14 MB/day
• Cost: MINIMAL
```

---

### 🔋 **Battery Impact:**

```
Modem Power Usage:
• Idle: 3mA
• Sending: 300mA (2 seconds)
• Average (old): ~45mA
• Average (new): ~12mA

Battery Savings:
• Old system: 1080mAh per 24h
• New system: 288mAh per 24h
• Savings: 792mAh (73% reduction!)
```

---

## 😴 SLEEP MODE

### How Sleep Mode Works

When vehicle is **parked and idle**, the system enters **power-saving mode**:

#### 💤 **Sleep Activation Conditions:**

```
ALL must be TRUE:
✅ No accident detected
✅ Not currently speeding
✅ No movement detected (2 minutes)
✅ No speed changes (2 minutes)
✅ No location changes (2 minutes)
✅ No significant tilt changes (2 minutes)

System Status:
📊 Idle Timer: 120 seconds (2 minutes)
⏱️ Last Activity: Track timestamp
🔋 Battery: Save power when possible
```

#### 🌙 **Sleep Cycle:**

```
┌────────────────────────────────────────┐
│  No Activity for 2 Minutes             │
│  → Enter SLEEP MODE                    │
└────────────────┬───────────────────────┘
                 │
                 ▼
┌────────────────────────────────────────┐
│  Light Sleep (10 seconds)              │
│  • CPU: Sleep                          │
│  • RAM: Active (state preserved)       │
│  • GPS: Reduced power                  │
│  • Modem: Idle                         │
│  • Power: ~15mA (vs 50mA active)       │
└────────────────┬───────────────────────┘
                 │
                 ▼
┌────────────────────────────────────────┐
│  Wake Up (automatic)                   │
│  • Read all sensors                    │
│  • Check GPS position                  │
│  • Check for changes                   │
└────────────────┬───────────────────────┘
                 │
        ┌────────┴────────┐
        │                 │
    ACTIVITY           NO ACTIVITY
    DETECTED           DETECTED
        │                 │
        ▼                 ▼
┌──────────────┐   ┌─────────────────┐
│  EXIT SLEEP  │   │  SLEEP AGAIN    │
│  Resume      │   │  (10 seconds)   │
│  normal      │   │  Repeat cycle   │
│  operation   │   │                 │
└──────────────┘   └─────────────────┘
```

---

### 🚨 **Sleep Mode NEVER Activates When:**

```
❌ Accident detected          → Emergency mode
❌ Currently speeding          → Safety monitoring active
❌ Cancel button pressed       → User interaction
❌ SMS being sent             → Communication active
❌ API upload in progress     → Data transmission
❌ Movement detected          → Vehicle in use
❌ Speed changing             → Vehicle accelerating/braking
❌ Recent significant change  → Activity detected
```

---

### ⚡ **Power Consumption Breakdown:**

```
ACTIVE MODE (Normal Operation):
┌──────────────────────────────────────┐
│ Component        Power    Duty Cycle │
├──────────────────────────────────────┤
│ ESP32 (active)   80mA     100%       │
│ ADXL345          140µA    100%       │
│ MPU6050          3.9mA    100%       │
│ GPS Module       25mA     100%       │
│ SIM A7670E       3-300mA  1-5%       │
├──────────────────────────────────────┤
│ TOTAL:           ~120mA average      │
└──────────────────────────────────────┘

SLEEP MODE (Light Sleep):
┌──────────────────────────────────────┐
│ Component        Power    Duty Cycle │
├──────────────────────────────────────┤
│ ESP32 (sleep)    800µA    100%       │
│ ADXL345          140µA    100%       │
│ MPU6050          10µA     100%       │
│ GPS Module       1mA      100%       │
│ SIM A7670E       3mA      100%       │
├──────────────────────────────────────┤
│ TOTAL:           ~5mA average        │
└──────────────────────────────────────┘

BATTERY LIFE (3000mAh LiPo):
• Active 24/7:   25 hours
• Sleep 8h/day:  50+ hours
• Parked vehicle: 3-4 days
```

---

### 📊 **Serial Monitor Output:**

```
ENTERING SLEEP:
💤 No activity for 2 minutes - Entering SLEEP MODE
   Device will wake on movement/GPS change/timer

😴 Sleeping for 10s...


WAKING UP:
👁️  Woke up - checking sensors
OK SAFE | Impact:1.01g | Tilt:0.5deg | Speed:0.0km/h | GPS:FIX(6) | HDOP:1.5 | Bat:3.9V | Idle:125s
😴 Sleeping for 10s...


ACTIVITY DETECTED:
👁️  Woke up - checking sensors
🔄 Significant change detected:
  🚗 Vehicle started moving
📤 Sending telemetry...
✅ Telemetry sent!

[SLEEP MODE EXITED - Normal operation resumed]
```

---

## 💡 LED INDICATORS

### LED Status Guide

```
┌─────────────────────────────────────────────────────────┐
│  GREEN LED (GPIO 25) - System Safe                      │
├─────────────────────────────────────────────────────────┤
│  ✅ Solid ON     → System operational, no issues        │
│  ⚪ OFF          → Accident detected / System problem   │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  RED LED (GPIO 14) - Danger / Accident                  │
├─────────────────────────────────────────────────────────┤
│  ⚪ OFF          → No accident                          │
│  🔴 Fast Blink   → Accident detected, waiting cancel    │
│                    (150ms on/off)                       │
│  🔴 Solid ON     → Emergency SMS sent                   │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  BLUE LED (GPIO 13) - GPS Status                        │
├─────────────────────────────────────────────────────────┤
│  🔵 Fast Blink   → GPS has fix (250ms interval)         │
│  🔵 Slow Blink   → GPS searching (1000ms interval)      │
│  ⚪ OFF          → GPS error / not initialized          │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  YELLOW LED (GPIO 23) - GSM/Modem Status                │
├─────────────────────────────────────────────────────────┤
│  🟡 Fast Blink   → Modem connected (300ms interval)     │
│  🟡 Slow Blink   → Modem not ready (1500ms interval)    │
│  ⚪ OFF          → Modem error                          │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  WHITE LED (Optional) - Power                           │
├─────────────────────────────────────────────────────────┤
│  ⚪ Solid ON     → System powered                       │
│  ⚪ OFF          → No power                             │
└─────────────────────────────────────────────────────────┘
```

### 🎬 **LED Scenarios:**

#### 🟢 Normal Operation
```
Green: ✅ Solid ON
Red:   ⚪ OFF
Blue:  🔵 Fast Blink (GPS locked)
Yellow: 🟡 Fast Blink (Modem ready)
```

#### 🔴 Accident Detected (Waiting Cancel)
```
Green: ⚪ OFF
Red:   🔴 Fast Blink (150ms)
Blue:  🔵 Fast/Slow Blink (GPS status)
Yellow: 🟡 Fast Blink
```

#### 🚨 Emergency SMS Sent
```
Green: ⚪ OFF
Red:   🔴 Solid ON
Blue:  🔵 Fast/Slow Blink
Yellow: 🟡 Fast Blink
```

#### 🛰️ GPS Searching (No Fix)
```
Green: ✅ Solid ON
Red:   ⚪ OFF
Blue:  🔵 Slow Blink (1000ms)
Yellow: 🟡 Fast/Slow Blink
```

#### 📶 Modem Not Ready
```
Green: ✅ Solid ON
Red:   ⚪ OFF
Blue:  🔵 Fast/Slow Blink
Yellow: 🟡 Slow Blink (1500ms)
```

---

## 🚑 EMERGENCY RESPONSE

### Complete Emergency Flow

```
┌─────────────────────────────────────────────────────────┐
│  ACCIDENT CONDITIONS MET                                │
│  (Impact + Vibration + Other sensors)                   │
└────────────────┬────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────┐
│  IMMEDIATE ACTIONS (T=0s)                               │
│  • Set accidentDetected = true                          │
│  • Record accident timestamp                            │
│  • Turn OFF green LED                                   │
│  • Start RED LED fast blinking                          │
│  • NO BUZZER (silent)                                   │
│  • Print accident details to Serial                     │
│  • Display: "Press CANCEL within 10s!"                  │
└────────────────┬────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────┐
│  10-SECOND CANCEL WINDOW                                │
│                                                         │
│  Option 1: OK Button Pressed                            │
│  ├─→ Cancel accident alert                              │
│  ├─→ Turn OFF red LED                                   │
│  ├─→ Turn ON green LED                                  │
│  ├─→ Reset all accident flags                           │
│  └─→ Return to normal operation                         │
│                                                         │
│  Option 2: 10 Seconds Expire (No Press)                 │
│  └─→ Continue to emergency response                     │
└────────────────┬────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────┐
│  SEND EMERGENCY SMS                                     │
│  • Set SMS mode (AT+CMGF=1)                             │
│  • Format emergency message:                            │
│    - Device ID                                          │
│    - Accident type                                      │
│    - Impact force (g)                                   │
│    - Time (UTC)                                         │
│    - GPS location (if available)                        │
│    - Speed at impact                                    │
│    - Google Maps link                                   │
│  • Send to: +2507929577181                              │
│  • Wait for confirmation                                │
│  • Set smsSent = true                                   │
│  • RED LED: Solid ON                                    │
└────────────────┬────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────┐
│  UPLOAD TO API (if GPRS connected)                      │
│  • Build telemetry JSON with all sensor data            │
│  • Include accident flag                                │
│  • POST to backend API                                  │
│  • Log in database for analysis                         │
│  • Await confirmation                                   │
└────────────────┬────────────────────────────────────────┘
                 │
                 ▼
┌─────────────────────────────────────────────────────────┐
│  EMERGENCY MODE ACTIVE                                  │
│  • System remains in alert state                        │
│  • Continue monitoring sensors                          │
│  • RED LED stays solid ON                               │
│  • Wait for manual reset or power cycle                 │
│  • Emergency contact can locate vehicle                 │
└─────────────────────────────────────────────────────────┘
```

---

### 📞 **Emergency Contact Receives:**

#### SMS Message:
```
!! VEHICLE ACCIDENT !!

Device: VEHICLE_001
Type: SEVERE_IMPACT
Impact: 18.45g
Time: 14:23:45

LOCATION:
Lat: -1.956789
Lon: 30.123456
Speed: 0.0 km/h
http://maps.google.com/maps?q=-1.956789,30.123456
```

#### Google Maps Link Opens:
- Exact location of accident
- Can navigate to location
- Can share with emergency services
- Can see nearby hospitals/police

---

## ⚙️ CONFIGURATION

### Customizable Settings

Edit these values in the code to customize behavior:

#### 🔧 **Device Configuration:**
```cpp
String DEVICE_ID = "VEHICLE_001";              // Unique device identifier
String EMERGENCY_CONTACT = "+2507929577181";   // Phone number for SMS
const char* APN = "internet.mtn.rw";           // Cellular network APN
```

#### 🏎️ **Speed Alert Settings:**
```cpp
const float SPEED_LIMIT = 60.0;                // Speed limit in km/h
const int MIN_SATELLITES_FOR_SPEED = 5;        // Min satellites for speed check
const float MIN_SPEED_THRESHOLD = 5.0;         // Ignore speeds below this (GPS noise)
const float MIN_GPS_HDOP = 2.5;                // Max HDOP for reliable speed
```

#### 💥 **Impact Detection Thresholds:**
```cpp
const float ADXL345_LOW_THRESHOLD = 3.0;       // Low impact (g-force)
const float ADXL345_MEDIUM_THRESHOLD = 8.0;    // Medium impact (g-force)
const float ADXL345_HIGH_THRESHOLD = 15.0;     // High impact (g-force)
const float MPU6050_TILT_THRESHOLD = 45.0;     // Tilt warning (degrees)
const float MPU6050_ROLLOVER_THRESHOLD = 70.0; // Rollover detection (degrees)
const float GYRO_ROTATION_THRESHOLD = 3.0;     // Rapid rotation (rad/s)
const float SPEED_DROP_THRESHOLD = 30.0;       // Sudden stop (km/h drop)
```

#### ⏰ **Timing Configuration:**
```cpp
const unsigned long GPS_WARMUP_TIME = 10000;         // GPS warm-up period (10s)
const unsigned long CANCEL_WINDOW = 10000;           // Time to cancel alert (10s)
const unsigned long API_SYNC_INTERVAL = 30000;       // Check changes every 30s
const unsigned long API_FORCE_SYNC_INTERVAL = 300000; // Force sync every 5min
const unsigned long SLEEP_IDLE_TIME = 120000;        // Sleep after 2min idle
```

#### 📊 **Change Detection Thresholds:**
```cpp
const float SPEED_CHANGE_THRESHOLD = 10.0;      // Speed change to trigger sync (km/h)
const float LOCATION_CHANGE_THRESHOLD = 0.0002; // Location change (~11 meters)
const float ACCEL_CHANGE_THRESHOLD = 2.0;       // Acceleration change (g-force)
const float ANGLE_CHANGE_THRESHOLD = 30.0;      // Angle change (degrees)
```

#### 🔋 **Battery Monitor Settings:**
```cpp
const float BATTERY_MIN_VOLTAGE = 3.3;          // Empty battery voltage
const float BATTERY_MAX_VOLTAGE = 4.2;          // Full battery voltage
const float BATTERY_DIVIDER = 2.0;              // Voltage divider ratio
```

---

## 🔍 TROUBLESHOOTING

### Common Issues & Solutions

#### 🐛 **Problem: Buzzer beeps immediately on startup**
```
Symptom: Buzzer beeps before GPS is ready
Cause:   GPS not warmed up yet
Solution: 
  • System waits 10 seconds for GPS warm-up
  • Speed alerts disabled during warm-up
  • Normal behavior, will stop after 10s
  • If continues, check GPS module connection
```

#### 🐛 **Problem: Speed alerts not working**
```
Symptom: No beep even when speeding
Possible Causes:
  1. GPS no fix
     → Move to open area with sky view
     → Check GPS LED (should be fast blinking)
  
  2. Not enough satellites
     → Need 5+ satellites
     → Check Serial: "Speed check: Not enough satellites"
  
  3. Poor GPS accuracy (high HDOP)
     → HDOP must be < 2.5
     → Check Serial: "Speed check: Poor GPS accuracy"
  
  4. Speed below threshold
     → Must exceed 60 km/h
     → Adjust SPEED_LIMIT if needed
  
  5. GPS module not connected
     → Check pins: GPIO 16/17
     → Check 5V power supply
```

#### 🐛 **Problem: False accident detections**
```
Symptom: Accidents detected from normal bumps
Solutions:
  1. Adjust vibration sensor sensitivity
     → Turn potentiometer clockwise (less sensitive)
     → Should NOT trigger from engine vibration
  
  2. Increase impact thresholds
     → Increase ADXL345_MEDIUM_THRESHOLD to 10.0g
     → Increase ADXL345_HIGH_THRESHOLD to 18.0g
  
  3. Check sensor mounting
     → Sensors must be firmly mounted
     → No loose connections
     → Vibration sensor should be on vehicle frame
```

#### 🐛 **Problem: No GPS fix**
```
Symptom: Blue LED slow blinking, no location
Solutions:
  1. Check antenna placement
     → Must have clear sky view
     → Away from metal objects
     → Near window or outside
  
  2. Wait for cold start
     → First GPS fix can take 5-15 minutes
     → Be patient on first boot
  
  3. Check connections
     → GPS TX → ESP32 GPIO 16
     → GPS RX → ESP32 GPIO 17
     → 5V power connected
  
  4. Check GPS module
     → Red LED on GPS should blink
     → If solid/off, module may be faulty
```

#### 🐛 **Problem: Modem not connecting**
```
Symptom: Yellow LED slow blinking, no GPRS
Solutions:
  1. Check SIM card
     → SIM inserted correctly
     → SIM activated and has credit/data
     → PIN disabled on SIM
  
  2. Check network
     → Wait 30-60 seconds for registration
     → Check signal strength in area
     → Try different APN settings
  
  3. Check connections
     → Modem RX → ESP32 GPIO 5
     → Modem TX → ESP32 GPIO 4
     → Power supply 3.8-4.2V, 2A capable
  
  4. Check modem commands
     → Open Serial Monitor
     → Look for "Modem initialized"
     → Check for "Network... OK"
```

#### 🐛 **Problem: Data not sending to API**
```
Symptom: "No significant change" message constantly
Solutions:
  1. Force sync by waiting
     → System sends every 5 minutes anyway
     → This is normal behavior!
  
  2. Create movement/change
     → Drive the vehicle (location change)
     → Accelerate/brake (speed change)
     → Go over bump (acceleration change)
  
  3. Check GPRS connection
     → Yellow LED should fast blink
     → Serial: "GPRS... OK"
  
  4. Check API endpoint
     → Verify API_URL is correct
     → Check backend server is running
     → Monitor Serial for "✅ SUCCESS"
```

#### 🐛 **Problem: MPU6050 not detected**
```
Symptom: "MPU6050... ! NOT DETECTED"
Solutions:
  1. Check AD0 pin
     → CRITICAL: AD0 must be connected to GND
     → This sets I2C address to 0x69
  
  2. Check I2C connections
     → SDA → GPIO 21
     → SCL → GPIO 22
     → Both sensors share same I2C bus
  
  3. Check I2C address
     → Run I2C scanner sketch
     → Should show 0x53 (ADXL345) and 0x69 (MPU6050)
  
  4. Non-critical error
     → System works without MPU6050
     → Only loses tilt/rollover detection
     → Impact detection still works
```

#### 🐛 **Problem: Sleep mode not activating**
```
Symptom: Device never enters sleep, "Idle" timer doesn't increase
Possible Causes:
  1. GPS constantly moving
     → Normal if vehicle is moving
     → Park vehicle to test sleep
  
  2. Speed changing
     → Normal in driving
     → Turn off engine to test
  
  3. Recent accident/alert
     → System stays awake after accident
     → Reset to clear
  
  4. Change threshold too low
     → Minor GPS drift prevents sleep
     → Normal behavior, prevents false wakes
```

---

### 📊 **Serial Monitor Debug Output**

#### Normal Operation:
```
OK SAFE | Impact:1.02g | Tilt:2.3deg | Speed:45.2km/h | GPS:FIX(7) | HDOP:1.2 | Bat:3.8V
```

#### GPS Searching:
```
OK SAFE | Impact:1.01g | Tilt:N/A | Speed:0.0km/h | GPS:NO(3) | HDOP:99.0 | Bat:3.9V
```

#### Speed Alert:
```
⚠️ WARNING: OVERSPEEDING!
Speed: 75.3 km/h
Limit: 60 km/h
Sats: 7 | HDOP: 1.2
GPS Quality: EXCELLENT
```

#### Accident Detection:
```
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
      ! ACCIDENT DETECTED !
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
Type: SEVERE_IMPACT
Severity: HIGH
Impact: 18.45g
Location: -1.956789, 30.123456

Press CANCEL within 10s!
!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
```

#### Data Sync:
```
🔄 Significant change detected:
  🏎️  Speed changed: 30.5 → 45.2 km/h
📤 Sending telemetry...
JSON: {"deviceId":"VEHICLE_001","latitude":-1.956789...
→ Sending to API...
   Payload: 287 bytes
   → Uploading JSON...
   → Data uploaded
   → POSTing... (30s)
.....
   Result: +HTTPACTION: 1,200,156
   ✅ SUCCESS!
✅ Telemetry sent!
```

#### Sleep Mode:
```
💤 No activity for 2 minutes - Entering SLEEP MODE
   Device will wake on movement/GPS change/timer

😴 Sleeping for 10s...
👁️  Woke up - checking sensors
OK SAFE | Impact:1.01g | Tilt:0.5deg | Speed:0.0km/h | GPS:FIX(6) | HDOP:1.5 | Bat:3.9V | Idle:125s
😴 Sleeping for 10s...
```

---

## 🎓 UNDERSTANDING THE SYSTEM

### Key Concepts

#### 1️⃣ **Multi-Sensor Fusion**
The system doesn't rely on ONE sensor - it uses MULTIPLE sensors together:
- ADXL345: Fast, sensitive impact detection
- MPU6050: Tilt and rollover angles
- SW-420: Physical vibration confirmation
- GPS: Speed and location
- All combined for accurate accident detection

#### 2️⃣ **Vibration Gate**
Why we need the vibration sensor:
- Prevents false positives
- Confirms physical impact
- Filters sensor noise
- Increases reliability

#### 3️⃣ **GPS Accuracy (HDOP)**
Lower HDOP = Better accuracy:
- 1.0 = Perfect for speed detection
- 2.5 = Maximum acceptable for speed alerts
- 5.0+ = Not reliable, speed alerts disabled

#### 4️⃣ **Smart Sync Benefits**
Only send data when needed:
- Saves 80% database space
- Reduces cellular costs
- Extends battery life
- Keeps important changes only

#### 5️⃣ **Sleep Mode Intelligence**
System knows when to sleep:
- Not during emergencies
- Not while speeding
- Not while moving
- Only when truly idle

---

## 📝 QUICK START CHECKLIST

### Before First Power-On:

```
Hardware:
☐ All sensors connected to correct pins
☐ MPU6050 AD0 pin connected to GND (CRITICAL!)
☐ GPS antenna has clear sky view
☐ SIM card inserted, activated, PIN disabled
☐ Battery connected and charged
☐ All ground connections secure
☐ Vibration sensor adjusted to medium sensitivity

Software:
☐ Arduino IDE installed
☐ ESP32 board support installed
☐ Required libraries installed:
   - Adafruit_Sensor
   - Adafruit_ADXL345_U
   - Adafruit_MPU6050
   - TinyGPSPlus
☐ DEVICE_ID configured
☐ EMERGENCY_CONTACT configured
☐ APN configured for your carrier
☐ API_URL configured

First Boot:
☐ Open Serial Monitor (115200 baud)
☐ Watch for "Sensors initialized"
☐ Watch for "Modem initialized"
☐ Watch for "GPRS... OK"
☐ Wait 10 seconds for GPS warm-up
☐ Verify LEDs: Green ON, Blue/Yellow blinking
☐ Test speed alert (drive >60 km/h)
☐ Test accident detection (shake device hard)
☐ Test cancel button
```

---

## 🆘 SUPPORT

### Need Help?

1. **Check Serial Monitor** - Most issues show debug messages
2. **Verify Connections** - 90% of issues are wiring
3. **Check Power Supply** - Modem needs 2A capability
4. **Read This Document** - Answer is probably here!
5. **Test Components** - Test each sensor individually

### Important Notes:

⚠️ **SAFETY FIRST:** This is a monitoring device, not a safety device. Always drive safely and follow traffic laws.

⚠️ **EMERGENCY CONTACTS:** Test SMS functionality before relying on it. Ensure emergency contact is correct.

⚠️ **GPS LOCATION:** GPS accuracy varies. Location is approximate, not exact.

⚠️ **BATTERY:** Monitor battery level. Replace when below 20%.

⚠️ **MOUNTING:** Secure device firmly. Loose mounting causes false detections.

---

## 📄 LICENSE & WARRANTY

This is open-source hardware/software provided AS-IS with no warranty. Use at your own risk. Test thoroughly before deployment.

---

**END OF DOCUMENTATION**

*L-GUARD v5.4 - Making Roads Safer* 🚗💚