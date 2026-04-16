/*
 * ACCIDENT LOGIC VALIDATOR
 * L-Guard v6.0 — No SMS, No API, No GPS
 * Just shows you every accident condition live as TRUE/FALSE
 * 
 * WHAT THIS TESTS:
 *   - ADXL375  → impact levels (8g / 25g / 50g / 100g)
 *   - MPU6050  → tilt angle (roll + pitch) + gyro spin
 *   - Combined → what accident type would fire RIGHT NOW
 * 
 * Wiring: same as your main project
 *   ADXL375 SDA → GPIO21 | SCL → GPIO22 | SDO → GND (addr 0x53)
 *   MPU6050 SDA → GPIO21 | SCL → GPIO22 | AD0 → VCC (addr 0x69)
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL375.h>
#include <Adafruit_MPU6050.h>

// ── Same addresses as your main code ──────────────────────────
#define ADXL375_ADDRESS  0x53
#define MPU6050_ADDRESS  0x69

// ── Same thresholds as your v6.0 ──────────────────────────────
const float THRESH_LOW         =  8.0;
const float THRESH_MEDIUM      = 25.0;
const float THRESH_HIGH        = 50.0;
const float THRESH_EXTREME     = 100.0;
const float TILT_WARN          = 45.0;   // MPU6050_TILT_THRESHOLD
const float ROLLOVER_TILT      = 70.0;   // MPU6050_ROLLOVER_THRESHOLD
const float GYRO_THRESH        =  3.0;   // GYRO_ROTATION_THRESHOLD (rad/s)
const int   ROLLOVER_HOLD      =  3;     // ROLLOVER_STABILITY_COUNT

// ── Improved rollover: needs impact too (the fix) ─────────────
// Set this to true to test the FIX vs the original bug
const bool  REQUIRE_IMPACT_FOR_ROLLOVER = true;
const float ROLLOVER_MIN_IMPACT = 5.0;   // at least 5g needed during rollover

Adafruit_ADXL375  accel = Adafruit_ADXL375(12345);
Adafruit_MPU6050  mpu;

bool mpuOK = false;

// ── State (same as main code) ──────────────────────────────────
float adxl_x, adxl_y, adxl_z, adxl_total;
float roll, pitch, totalGyro;
int   rolloverCount = 0;

// ── Calibration offsets ────────────────────────────────────────
float rollOffset = 0, pitchOffset = 0;

// ── Peak tracking ──────────────────────────────────────────────
float peakG     = 0;
float peakRoll  = 0;
float peakGyro  = 0;

// ─────────────────────────────────────────────────────────────
// Shared result type for evaluateAccident()
// Moved here to avoid Arduino/IDE prototype-generation ordering issues
struct AccidentResult {
  String type;
  String level;
  bool   fired;
};

unsigned long lastPrint = 0;
const unsigned long PRINT_MS = 200; // print 5x per second

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire.begin(21, 22);

  Serial.println("\n================================================");
  Serial.println("   ACCIDENT LOGIC VALIDATOR — L-Guard v6.0");
  Serial.println("================================================\n");

  // Init ADXL375
  Serial.print("ADXL375... ");
  if (!accel.begin(ADXL375_ADDRESS)) {
    Serial.println("FAILED — check wiring");
    while(1) delay(500);
  }
  accel.setDataRate(ADXL343_DATARATE_100_HZ);
  Serial.println("OK");

  // Init MPU6050
  Serial.print("MPU6050... ");
  if (!mpu.begin(MPU6050_ADDRESS)) {
    Serial.println("NOT FOUND — tilt/rollover tests disabled");
    mpuOK = false;
  } else {
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    mpuOK = true;
    Serial.println("OK");

    // Calibrate — keep device still!
    Serial.print("Calibrating MPU6050 (keep still)... ");
    delay(800);
    float sumR = 0, sumP = 0;
    for (int i = 0; i < 20; i++) {
      sensors_event_t a, g, t;
      mpu.getEvent(&a, &g, &t);
      sumP += atan2(a.acceleration.y,
                   sqrt(a.acceleration.x * a.acceleration.x +
                        a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
      sumR += atan2(-a.acceleration.x, a.acceleration.z) * 180.0 / PI;
      delay(30);
    }
    rollOffset  = sumR / 20.0;
    pitchOffset = sumP / 20.0;
    Serial.println("done");
    Serial.print("  Roll offset:  "); Serial.println(rollOffset, 2);
    Serial.print("  Pitch offset: "); Serial.println(pitchOffset, 2);
  }

  Serial.println();
  Serial.print("Rollover fix (needs impact too): ");
  Serial.println(REQUIRE_IMPACT_FOR_ROLLOVER ? "ENABLED" : "disabled (original bug)");

  Serial.println("\n--- WHAT TO DO ---");
  Serial.println("1. Tap/knock sensor → watch ADXL conditions");
  Serial.println("2. Tilt device 45°  → TILT_WARN should go TRUE");
  Serial.println("3. Tilt device 70°+ → rolloverCount should increment");
  Serial.println("4. Tilt 70° + knock → ROLLOVER should fire (with fix)");
  Serial.println("5. Spin/rotate fast → GYRO should go TRUE");
  Serial.println("------------------\n");
  delay(1000);
}

// ─────────────────────────────────────────────────────────────
void readADXL() {
  sensors_event_t e;
  accel.getEvent(&e);
  const float C = 1.0 / 9.80665;
  adxl_x = e.acceleration.x * C;
  adxl_y = e.acceleration.y * C;
  adxl_z = e.acceleration.z * C;
  adxl_total = sqrt(adxl_x*adxl_x + adxl_y*adxl_y + adxl_z*adxl_z);
  if (adxl_total > peakG) peakG = adxl_total;
}

void readMPU() {
  if (!mpuOK) return;
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  float rawPitch = atan2(a.acceleration.y,
                         sqrt(a.acceleration.x * a.acceleration.x +
                              a.acceleration.z * a.acceleration.z)) * 180.0 / PI;
  float rawRoll  = atan2(-a.acceleration.x, a.acceleration.z) * 180.0 / PI;

  pitch = rawPitch - pitchOffset;
  roll  = rawRoll  - rollOffset;

  float gx = g.gyro.x, gy = g.gyro.y, gz = g.gyro.z;
  totalGyro = sqrt(gx*gx + gy*gy + gz*gz);

  // Rollover counter — same logic as main code
  if (abs(roll) > ROLLOVER_TILT || abs(pitch) > ROLLOVER_TILT)
    rolloverCount = constrain(rolloverCount + 1, 0, ROLLOVER_HOLD);
  else
    rolloverCount = 0;

  if (abs(roll)     > peakRoll)  peakRoll  = abs(roll);
  if (totalGyro     > peakGyro)  peakGyro  = totalGyro;
}

// ─────────────────────────────────────────────────────────────
// THE ACCIDENT CONDITIONS — identical to checkAccident() in v6.0
// Returns what would fire, or "NONE"
AccidentResult evaluateAccident() {
  // ── ADXL conditions ──
  bool extremeImpact = (adxl_total > THRESH_EXTREME);
  bool highImpact    = (adxl_total > THRESH_HIGH);
  bool mediumImpact  = (adxl_total > THRESH_MEDIUM);
  bool lowImpact     = (adxl_total > THRESH_LOW);

  // ── MPU conditions ──
  bool rolloverSustained, tiltWarning, rapidRotation;
  if (mpuOK) {
    // ORIGINAL: rollover fires on tilt alone
    bool tiltThreshold = (abs(roll) > ROLLOVER_TILT || abs(pitch) > ROLLOVER_TILT);

    if (REQUIRE_IMPACT_FOR_ROLLOVER) {
      // FIXED: rollover needs tilt + some impact
      rolloverSustained = (rolloverCount >= ROLLOVER_HOLD) && (adxl_total > ROLLOVER_MIN_IMPACT);
    } else {
      // ORIGINAL BUG: tilt alone = rollover
      rolloverSustained = (rolloverCount >= ROLLOVER_HOLD);
    }

    tiltWarning   = (abs(roll) > TILT_WARN || abs(pitch) > TILT_WARN);
    rapidRotation = (totalGyro > GYRO_THRESH);
  } else {
    rolloverSustained = false;
    tiltWarning       = false;
    rapidRotation     = false;
  }

  // ── Priority order (same as your main code) ──
  if (extremeImpact)
    return {"CATASTROPHIC_IMPACT", "EXTREME", true};

  if (highImpact)
    return {"SEVERE_IMPACT", "HIGH", true};

  if (rolloverSustained)
    return {"ROLLOVER", "HIGH", true};

  if (mediumImpact)
    return {"IMPACT_COLLISION", "MEDIUM", true};

  if (tiltWarning && rapidRotation)
    return {"VEHICLE_FLIP", "MEDIUM", true};

  return {"NONE", "NONE", false};
}

// ─────────────────────────────────────────────────────────────
void printDashboard() {
  AccidentResult result = evaluateAccident();

  // Condition flags
  bool b_extreme    = adxl_total > THRESH_EXTREME;
  bool b_high       = adxl_total > THRESH_HIGH;
  bool b_medium     = adxl_total > THRESH_MEDIUM;
  bool b_low        = adxl_total > THRESH_LOW;
  bool b_rollover   = rolloverCount >= ROLLOVER_HOLD;
  bool b_tilt       = mpuOK && (abs(roll) > TILT_WARN || abs(pitch) > TILT_WARN);
  bool b_gyro       = mpuOK && (totalGyro > GYRO_THRESH);
  bool b_roll_tilt  = mpuOK && (abs(roll) > ROLLOVER_TILT || abs(pitch) > ROLLOVER_TILT);

  Serial.println("\n╔═══════════════════════════════════════════╗");
  Serial.println("║         ACCIDENT LOGIC VALIDATOR          ║");
  Serial.println("╠═══════════════════════════════════════════╣");

  // Raw values
  Serial.printf("║ ADXL375   X:%+6.2f Y:%+6.2f Z:%+6.2f     ║\n", adxl_x, adxl_y, adxl_z);
  Serial.printf("║ Total: %5.2fg   Peak: %5.2fg              ║\n", adxl_total, peakG);

  if (mpuOK) {
    Serial.printf("║ MPU6050   Roll:%+6.1f°  Pitch:%+6.1f°      ║\n", roll, pitch);
    Serial.printf("║ Gyro: %5.2f rad/s   RollCnt: %d/%d         ║\n", totalGyro, rolloverCount, ROLLOVER_HOLD);
  } else {
    Serial.println("║ MPU6050: NOT CONNECTED                    ║");
  }

  Serial.println("╠═══════════════════════════════════════════╣");
  Serial.println("║  CONDITION              STATUS            ║");
  Serial.println("║  ─────────────────────────────────────    ║");
  Serial.printf( "║  ADXL > 8g  (low)       %s            ║\n", b_low     ? "TRUE ⚡" : "false  ");
  Serial.printf( "║  ADXL > 25g (medium)    %s            ║\n", b_medium  ? "TRUE ⚡" : "false  ");
  Serial.printf( "║  ADXL > 50g (high)      %s            ║\n", b_high    ? "TRUE ⚡" : "false  ");
  Serial.printf( "║  ADXL > 100g (extreme)  %s            ║\n", b_extreme ? "TRUE ⚡" : "false  ");
  Serial.printf( "║  Tilt > 45° (warn)      %s            ║\n", b_tilt    ? "TRUE ⚡" : "false  ");
  Serial.printf( "║  Tilt > 70° (rollover)  %s            ║\n", b_roll_tilt ? "TRUE ⚡" : "false  ");
  Serial.printf( "║  Gyro > 3 rad/s         %s            ║\n", b_gyro    ? "TRUE ⚡" : "false  ");
  Serial.printf( "║  RolloverCount >= 3     %s            ║\n", b_rollover ? "TRUE ⚡" : "false  ");

  Serial.println("╠═══════════════════════════════════════════╣");

  if (result.fired) {
    Serial.println("║  !! ACCIDENT WOULD FIRE !!                ║");
    Serial.printf( "║  Type:  %-34s║\n", result.type.c_str());
    Serial.printf( "║  Level: %-34s║\n", result.level.c_str());
  } else {
    Serial.println("║  Result: NO ACCIDENT                      ║");
    Serial.println("║  System nominal                           ║");
  }

  Serial.println("╚═══════════════════════════════════════════╝");
}

// ─────────────────────────────────────────────────────────────
void loop() {
  readADXL();
  readMPU();

  unsigned long now = millis();
  if (now - lastPrint >= PRINT_MS) {
    lastPrint = now;
    printDashboard();
  }

  // Slowly decay peak every 10 seconds so each test run is fresh
  static unsigned long lastDecay = 0;
  if (now - lastDecay > 10000) {
    peakG    = max(1.0f, peakG    * 0.5f);
    peakRoll = max(0.0f, peakRoll * 0.5f);
    peakGyro = max(0.0f, peakGyro * 0.5f);
    lastDecay = now;
  }

  delay(8); // ~100Hz sensor loop
}
