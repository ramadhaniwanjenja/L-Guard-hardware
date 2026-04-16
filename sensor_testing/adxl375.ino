/*
 * ADXL375 STANDALONE TEST SKETCH
 * L-Guard Threshold Validation Tool
 * 
 * Wiring (ESP32):
 *   VCC → 3.3V
 *   GND → GND
 *   SDA → GPIO21
 *   SCL → GPIO22
 *   CS  → 3.3V (I2C mode)
 *   ALT ADDRESS (SDO) → GND → I2C address = 0x53
 * 
 * Open Serial Monitor at 115200 baud
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL375.h>

#define ADXL375_ADDRESS 0x53

// ── Thresholds matching your v6.0 config ──────────────────────
const float THRESH_LOW     =  8.0;   // Low impact
const float THRESH_MEDIUM  = 25.0;   // Medium impact / collision
const float THRESH_HIGH    = 50.0;   // Severe impact
const float THRESH_EXTREME = 100.0;  // Catastrophic impact

// ── Tuning ─────────────────────────────────────────────────────
const unsigned long PRINT_INTERVAL = 100;   // ms between readings (10 Hz)
const float         PEAK_DECAY     = 0.005; // How fast peak fades per loop tick

Adafruit_ADXL375 accel = Adafruit_ADXL375(12345);

float peakTotal = 0;
unsigned long lastPrint = 0;
unsigned long testCount = 0;

// ── Classify impact ────────────────────────────────────────────
String classifyImpact(float g) {
  if (g > THRESH_EXTREME) return "*** EXTREME (>100g) ***";
  if (g > THRESH_HIGH)    return "**  HIGH    (>50g)  **";
  if (g > THRESH_MEDIUM)  return "*   MEDIUM  (>25g)   *";
  if (g > THRESH_LOW)     return "    LOW     (>8g)    ";
  return "    (below threshold)  ";
}

// ── Draw a simple bar ──────────────────────────────────────────
String bar(float g, float maxG = 100.0, int width = 30) {
  int filled = constrain((int)((g / maxG) * width), 0, width);
  String b = "[";
  for (int i = 0; i < width; i++) b += (i < filled) ? "#" : "-";
  b += "]";
  return b;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n============================================");
  Serial.println("   ADXL375 THRESHOLD TEST  (L-Guard v6.0)");
  Serial.println("============================================");
  Serial.println("Thresholds:");
  Serial.println("  LOW     > 8g");
  Serial.println("  MEDIUM  > 25g");
  Serial.println("  HIGH    > 50g");
  Serial.println("  EXTREME > 100g");
  Serial.println("--------------------------------------------\n");

  Wire.begin(21, 22);

  Serial.print("Connecting to ADXL375 at 0x53 ... ");
  if (!accel.begin(ADXL375_ADDRESS)) {
    Serial.println("FAILED");
    Serial.println("\nCheck wiring and ALT ADDRESS pin.");
    while (1) { delay(500); }
  }
  Serial.println("OK");

  accel.setDataRate(ADXL343_DATARATE_100_HZ);

  Serial.println("Data rate: 100 Hz");
  Serial.println("Range: ±200g (fixed)");
  Serial.println("\nStarting readings... hit your sensor!\n");
  Serial.println("  X(g)    Y(g)    Z(g)  | Total  | Peak   | Level");
  Serial.println("------------------------------------------------------");
}

void loop() {
  sensors_event_t event;
  accel.getEvent(&event);

  const float MPS2_TO_G = 1.0 / 9.80665;
  float x = event.acceleration.x * MPS2_TO_G;
  float y = event.acceleration.y * MPS2_TO_G;
  float z = event.acceleration.z * MPS2_TO_G;
  float total = sqrt(x*x + y*y + z*z);

  // Update peak
  if (total > peakTotal) {
    peakTotal = total;
  } else {
    peakTotal = max(0.0f, peakTotal - PEAK_DECAY);
  }

  // Print at set interval
  unsigned long now = millis();
  if (now - lastPrint >= PRINT_INTERVAL) {
    lastPrint = now;

    // Compact row
    char buf[80];
    snprintf(buf, sizeof(buf),
      "%+7.2f %+7.2f %+7.2f | %6.2fg | %6.2fg | %s",
      x, y, z, total, peakTotal, classifyImpact(total).c_str()
    );
    Serial.println(buf);

    // Print bar graph every 500 ms so it doesn't flood
    if ((now / 500) % 2 == 0) {
      Serial.print("  Total: ");
      Serial.print(bar(total));
      Serial.print("  ");
      Serial.print(total, 1);
      Serial.println("g");
    }

    // Print threshold crossed alert
    if (total > THRESH_EXTREME) {
      Serial.println("\n  !!!! EXTREME IMPACT DETECTED !!!!");
      Serial.print("  Value: "); Serial.print(total, 2); Serial.println("g");
      Serial.println("  → accidentType = CATASTROPHIC_IMPACT\n");
    } else if (total > THRESH_HIGH) {
      Serial.println("\n  !! HIGH IMPACT DETECTED !!");
      Serial.print("  Value: "); Serial.print(total, 2); Serial.println("g");
      Serial.println("  → accidentType = SEVERE_IMPACT\n");
    } else if (total > THRESH_MEDIUM) {
      Serial.println("\n  ! MEDIUM IMPACT DETECTED !");
      Serial.print("  Value: "); Serial.print(total, 2); Serial.println("g");
      Serial.println("  → accidentType = IMPACT_COLLISION\n");
    } else if (total > THRESH_LOW) {
      Serial.println("  [low impact]");
    }
  }

  // Every 5 seconds print a summary
  if (millis() / 5000 != testCount) {
    testCount = millis() / 5000;
    Serial.println("\n--- 5s SUMMARY ---");
    Serial.print("  Peak so far: ");
    Serial.print(peakTotal, 2);
    Serial.println("g");
    Serial.println("  Type: " + classifyImpact(peakTotal));
    Serial.println("------------------\n");
    peakTotal *= 0.5; // Soft-reset peak so each window is fresh
  }

  delay(5); // ~100Hz effective rate
}
