/*
 * ADXL375 STANDALONE TEST SKETCH
 * L-Guard High-G Peak Capture Test
 *
 * Purpose:
 * - Confirm ADXL375 is working
 * - Capture true peak impact
 * - Check if impact exceeds 16g to confirm it is not behaving like ADXL345
 *
 * Wiring ESP32:
 *   VCC → 3.3V
 *   GND → GND
 *   SDA → GPIO21
 *   SCL → GPIO22
 *   CS  → 3.3V for I2C mode
 *   ALT ADDRESS / SDO → GND for I2C address 0x53
 *
 * Serial Monitor: 115200 baud
 */

#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL375.h>

#define ADXL375_ADDRESS 0x53

const float THRESH_LOW     = 8.0;
const float THRESH_MEDIUM  = 25.0;
const float THRESH_HIGH    = 50.0;
const float THRESH_EXTREME = 100.0;

const unsigned long PRINT_INTERVAL = 500; // print every 500ms
const float MPS2_TO_G = 1.0 / 9.80665;

Adafruit_ADXL375 accel = Adafruit_ADXL375(12345);

float currentX = 0;
float currentY = 0;
float currentZ = 0;
float currentTotal = 0;

float peakX = 0;
float peakY = 0;
float peakZ = 0;
float peakTotal = 0;

unsigned long lastPrint = 0;
unsigned long sampleCount = 0;

String classifyImpact(float g) {
  if (g > THRESH_EXTREME) return "*** EXTREME (>100g) ***";
  if (g > THRESH_HIGH)    return "** HIGH (>50g) **";
  if (g > THRESH_MEDIUM)  return "* MEDIUM (>25g) *";
  if (g > THRESH_LOW)     return "LOW (>8g)";
  return "below threshold";
}

String bar(float g, float maxG = 100.0, int width = 30) {
  int filled = constrain((int)((g / maxG) * width), 0, width);
  String b = "[";
  for (int i = 0; i < width; i++) {
    b += (i < filled) ? "#" : "-";
  }
  b += "]";
  return b;
}

void resetPeak() {
  peakX = 0;
  peakY = 0;
  peakZ = 0;
  peakTotal = 0;
  sampleCount = 0;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n============================================");
  Serial.println("   ADXL375 HIGH-G PEAK CAPTURE TEST");
  Serial.println("============================================");

  Wire.begin(21, 22);

  Serial.print("Connecting to ADXL375 at 0x53 ... ");
  if (!accel.begin(ADXL375_ADDRESS)) {
    Serial.println("FAILED");
    Serial.println("Check wiring, power, SDA/SCL, CS, and ALT ADDRESS pin.");
    while (1) {
      delay(500);
    }
  }

  Serial.println("OK");

  // Higher data rate to catch short impact spikes
  accel.setDataRate(ADXL343_DATARATE_800_HZ);

  Serial.println("Data rate: 800 Hz");
  Serial.println("Range: ±200g fixed");
  Serial.println("--------------------------------------------");
  Serial.println("Test steps:");
  Serial.println("1. Keep device still: Total should be about 1g");
  Serial.println("2. Rotate device: one axis should move near ±1g");
  Serial.println("3. Tap device firmly: Peak should rise");
  Serial.println("4. If peak exceeds 16g, it is NOT behaving like ADXL345");
  Serial.println("--------------------------------------------\n");

  Serial.println("Current values print every 500ms.");
  Serial.println("Short impact peaks are captured between prints.\n");
}

void loop() {
  sensors_event_t event;
  accel.getEvent(&event);

  currentX = event.acceleration.x * MPS2_TO_G;
  currentY = event.acceleration.y * MPS2_TO_G;
  currentZ = event.acceleration.z * MPS2_TO_G;

  currentTotal = sqrt(
    currentX * currentX +
    currentY * currentY +
    currentZ * currentZ
  );

  sampleCount++;

  // Capture true peak between print intervals
  if (currentTotal > peakTotal) {
    peakTotal = currentTotal;
    peakX = currentX;
    peakY = currentY;
    peakZ = currentZ;
  }

  unsigned long now = millis();

  if (now - lastPrint >= PRINT_INTERVAL) {
    lastPrint = now;

    Serial.println("--------------------------------------------------");

    Serial.print("Current X:");
    Serial.print(currentX, 2);
    Serial.print("g Y:");
    Serial.print(currentY, 2);
    Serial.print("g Z:");
    Serial.print(currentZ, 2);
    Serial.print("g | Total:");
    Serial.print(currentTotal, 2);
    Serial.println("g");

    Serial.print("PEAK    X:");
    Serial.print(peakX, 2);
    Serial.print("g Y:");
    Serial.print(peakY, 2);
    Serial.print("g Z:");
    Serial.print(peakZ, 2);
    Serial.print("g | Peak Total:");
    Serial.print(peakTotal, 2);
    Serial.println("g");

    Serial.print("Level: ");
    Serial.println(classifyImpact(peakTotal));

    Serial.print("Bar: ");
    Serial.print(bar(peakTotal));
    Serial.print(" ");
    Serial.print(peakTotal, 1);
    Serial.println("g");

    Serial.print("Samples captured in window: ");
    Serial.println(sampleCount);

    if (peakTotal > 16.0) {
      Serial.println("✅ Peak above 16g captured: this is NOT behaving like ADXL345.");
    }

    if (peakTotal > THRESH_EXTREME) {
      Serial.println("!!!! EXTREME IMPACT DETECTED !!!!");
    } else if (peakTotal > THRESH_HIGH) {
      Serial.println("!! HIGH IMPACT DETECTED !!");
    } else if (peakTotal > THRESH_MEDIUM) {
      Serial.println("! MEDIUM IMPACT DETECTED !");
    } else if (peakTotal > THRESH_LOW) {
      Serial.println("[low impact]");
    }

    resetPeak();
  }

  // No delay, so we do not miss short impact peaks
}
