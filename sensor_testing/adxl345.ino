/*
 * ADXL345 IMPACT TESTER - Simple Test Code
 * Tests impact detection levels without other sensors
 */

 #include <Wire.h>
 #include <Adafruit_Sensor.h>
 #include <Adafruit_ADXL345_U.h>
 
 // Create ADXL345 object
 Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);
 
 // Impact thresholds (adjust these to test)
 const float LOW_THRESHOLD = 3.0;      // Low impact (g)
 const float MEDIUM_THRESHOLD = 8.0;   // Medium impact (g)
 const float HIGH_THRESHOLD = 15.0;    // High impact (g)
 
 // Variables
 float adxl345X = 0, adxl345Y = 0, adxl345Z = 0;
 float adxl345Total = 0;
 float maxImpact = 0;
 
 void setup() {
   Serial.begin(115200);
   delay(2000);
   
   Serial.println("\n=================================");
   Serial.println("  ADXL345 IMPACT TESTER v1.0");
   Serial.println("=================================\n");
   
   // Initialize I2C
   Wire.begin(21, 22);  // SDA=21, SCL=22 for ESP32
   
   // Initialize ADXL345
   Serial.print("Initializing ADXL345... ");
   if (!accel.begin()) {
     Serial.println("FAILED!");
     Serial.println("Check connections:");
     Serial.println("  SDA -> GPIO 21");
     Serial.println("  SCL -> GPIO 22");
     Serial.println("  VCC -> 3.3V");
     Serial.println("  GND -> GND");
     while (1) {
       delay(1000);
     }
   }
   Serial.println("OK!\n");
   
   // Configure sensor
   accel.setRange(ADXL345_RANGE_16_G);
   accel.setDataRate(ADXL345_DATARATE_400_HZ);
   
   // Display thresholds
   Serial.println("Impact Thresholds:");
   Serial.println("  LOW    : " + String(LOW_THRESHOLD) + "g");
   Serial.println("  MEDIUM : " + String(MEDIUM_THRESHOLD) + "g");
   Serial.println("  HIGH   : " + String(HIGH_THRESHOLD) + "g");
   Serial.println("\n=================================");
   Serial.println("Ready! Tap/shake sensor to test");
   Serial.println("=================================\n");
   
   delay(1000);
 }
 
 void loop() {
   // Read sensor
   sensors_event_t event;
   accel.getEvent(&event);
   
   // Convert to G-force (m/s² to g)
   const float MPS2_TO_G = 1.0 / 9.80665;
   adxl345X = event.acceleration.x * MPS2_TO_G;
   adxl345Y = event.acceleration.y * MPS2_TO_G;
   adxl345Z = event.acceleration.z * MPS2_TO_G;
   
   // Calculate total force
   adxl345Total = sqrt(adxl345X * adxl345X + 
                       adxl345Y * adxl345Y + 
                       adxl345Z * adxl345Z);
   
   // Track maximum impact
   if (adxl345Total > maxImpact) {
     maxImpact = adxl345Total;
   }
   
   // Check thresholds and print
   if (adxl345Total > HIGH_THRESHOLD) {
     Serial.println("🔴 HIGH IMPACT! Force: " + String(adxl345Total, 2) + "g");
     printDetails();
     delay(500);  // Debounce
     
   } else if (adxl345Total > MEDIUM_THRESHOLD) {
     Serial.println("🟡 MEDIUM Impact: " + String(adxl345Total, 2) + "g");
     printDetails();
     delay(300);  // Debounce
     
   } else if (adxl345Total > LOW_THRESHOLD) {
     Serial.println("🟢 Low impact: " + String(adxl345Total, 2) + "g");
     printDetails();
     delay(200);  // Debounce
     
   } else {
     // Normal monitoring (print every 2 seconds)
     static unsigned long lastPrint = 0;
     if (millis() - lastPrint > 2000) {
       Serial.print("Current: ");
       Serial.print(adxl345Total, 2);
       Serial.print("g  |  Max: ");
       Serial.print(maxImpact, 2);
       Serial.println("g");
       lastPrint = millis();
     }
   }
   
   delay(50);  // 20Hz update rate
 }
 
 void printDetails() {
   Serial.println("  X: " + String(adxl345X, 2) + "g");
   Serial.println("  Y: " + String(adxl345Y, 2) + "g");
   Serial.println("  Z: " + String(adxl345Z, 2) + "g");
   Serial.println("  MAX: " + String(maxImpact, 2) + "g");
   Serial.println("---");
 }
 