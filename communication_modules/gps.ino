/*
 * GP-02 GPS MODULE - WORKING CODE
 * 
 * Hardware: GP-02 GPS (L86M33 compatible)
 * Library: TinyGPSPlus
 * 
 * Features:
 * - Real-time location (latitude, longitude)
 * - Altitude
 * - Speed (km/h)
 * - Heading/Course
 * - Satellites count
 * - UTC Time
 * - Fix quality
 */

 #include <TinyGPSPlus.h>

 // ==================== GPS PIN CONFIGURATION ====================
 #define GPS_MODULE_TX      16    // GPS TX -> ESP32 RX
 #define GPS_MODULE_RX      17    // GPS RX <- ESP32 TX
 
 // ==================== OPTIONAL LED ====================
 #define GPS_LED_PIN        13    // Optional: LED to show GPS status
 
 // ==================== GPS OBJECT ====================
 TinyGPSPlus gps;
 HardwareSerial gpsSerial(2);  // Using UART2
 
 // ==================== GPS DATA VARIABLES ====================
 float currentLat = 0;
 float currentLon = 0;
 float altitude = 0;
 float currentSpeed = 0;
 float heading = 0;
 int satellites = 0;
 bool gpsFixed = false;
 String utcTime = "";
 
 // ==================== TIMING ====================
 const unsigned long GPS_UPDATE_INTERVAL = 1000;  // Update every 1 second
 unsigned long lastGpsUpdate = 0;
 
 // LED blink
 unsigned long lastGpsBlinkTime = 0;
 bool gpsLedState = false;
 
 // ==================== SETUP ====================
 
 void setup() {
   Serial.begin(115200);
   delay(2000);
   
   Serial.println("\n╔════════════════════════════════════════╗");
   Serial.println("║     GP-02 GPS MODULE - TEST CODE      ║");
   Serial.println("╚════════════════════════════════════════╝\n");
   
   // Setup LED (optional)
   if (GPS_LED_PIN >= 0) {
     pinMode(GPS_LED_PIN, OUTPUT);
     digitalWrite(GPS_LED_PIN, LOW);
   }
   
   // Initialize GPS
   setupGPS();
   
   Serial.println("✅ GPS initialized!");
   Serial.println("📡 Waiting for GPS fix...");
   Serial.println("   (This may take 30-60 seconds outdoors)\n");
   Serial.println("═══════════════════════════════════════════\n");
 }
 
 // ==================== MAIN LOOP ====================
 
 void loop() {
   unsigned long currentMillis = millis();
   
   // Read GPS data
   if (currentMillis - lastGpsUpdate >= GPS_UPDATE_INTERVAL) {
     readGPS();
     printGPSData();
     lastGpsUpdate = currentMillis;
   }
   
   // Update GPS LED
   updateGPSLED();
   
   delay(10);
 }
 
 // ==================== GPS FUNCTIONS ====================
 
 void setupGPS() {
   Serial.println("→ Initializing GP-02 GPS...");
   gpsSerial.begin(9600, SERIAL_8N1, GPS_MODULE_TX, GPS_MODULE_RX);
   delay(1000);
 }
 
 void readGPS() {
   // Read all available GPS data
   while (gpsSerial.available() > 0) {
     char c = gpsSerial.read();
     gps.encode(c);
   }
 
   // Update location
   if (gps.location.isUpdated()) {
     currentLat = gps.location.lat();
     currentLon = gps.location.lng();
     gpsFixed = gps.location.isValid();
   }
 
   // Update altitude
   if (gps.altitude.isUpdated()) {
     altitude = gps.altitude.meters();
   }
 
   // Update speed (accurate from GPS!)
   if (gps.speed.isUpdated()) {
     currentSpeed = gps.speed.kmph();
   }
   
   // Update heading/course
   if (gps.course.isUpdated()) {
     heading = gps.course.deg();
   }
 
   // Update satellites
   if (gps.satellites.isUpdated()) {
     satellites = gps.satellites.value();
   }
 
   // Update time
   if (gps.time.isValid()) {
     char timeStr[16];
     sprintf(timeStr, "%02d:%02d:%02d", 
             gps.time.hour(), 
             gps.time.minute(), 
             gps.time.second());
     utcTime = String(timeStr);
   }
 }
 
 void printGPSData() {
   Serial.println("═══════════════════════════════════════════");
   
   if (gpsFixed) {
     Serial.println("📍 GPS FIX ACQUIRED!");
     Serial.println("───────────────────────────────────────────");
     
     // Location
     Serial.print("   Latitude:  ");
     Serial.println(currentLat, 6);
     
     Serial.print("   Longitude: ");
     Serial.println(currentLon, 6);
     
     Serial.print("   Altitude:  ");
     Serial.print(altitude, 1);
     Serial.println(" m");
     
     // Motion
     Serial.print("   Speed:     ");
     Serial.print(currentSpeed, 1);
     Serial.println(" km/h");
     
     Serial.print("   Heading:   ");
     Serial.print(heading, 1);
     Serial.println(" °");
     
     // Quality
     Serial.print("   Satellites: ");
     Serial.println(satellites);
     
     Serial.print("   Time (UTC): ");
     Serial.println(utcTime);
     
     // Google Maps link
     Serial.println("\n   🗺️  Google Maps:");
     Serial.print("   http://maps.google.com/maps?q=");
     Serial.print(currentLat, 6);
     Serial.print(",");
     Serial.println(currentLon, 6);
     
   } else {
     Serial.println("⏳ WAITING FOR GPS FIX...");
     Serial.println("───────────────────────────────────────────");
     Serial.print("   Satellites visible: ");
     Serial.println(satellites);
     Serial.println("   💡 Tips:");
     Serial.println("   • Go outdoors (clear sky view)");
     Serial.println("   • Wait 30-60 seconds");
     Serial.println("   • Avoid buildings/trees");
   }
   
   Serial.println("═══════════════════════════════════════════\n");
 }
 
 void updateGPSLED() {
   if (GPS_LED_PIN < 0) return;
   
   unsigned long currentMillis = millis();
   
   // Blink pattern based on GPS status
   // Fast blink (250ms) = Has fix
   // Slow blink (1000ms) = Searching
   unsigned long blinkInterval = gpsFixed ? 250 : 1000;
   
   if (currentMillis - lastGpsBlinkTime >= blinkInterval) {
     gpsLedState = !gpsLedState;
     digitalWrite(GPS_LED_PIN, gpsLedState);
     lastGpsBlinkTime = currentMillis;
   }
 }
 
 // ==================== HELPER FUNCTIONS ====================
 
 // Get distance between two GPS coordinates (in meters)
 double getDistance(double lat1, double lon1, double lat2, double lon2) {
   return TinyGPSPlus::distanceBetween(lat1, lon1, lat2, lon2);
 }
 
 // Get bearing/direction to another point (in degrees)
 double getBearing(double lat1, double lon1, double lat2, double lon2) {
   return TinyGPSPlus::courseTo(lat1, lon1, lat2, lon2);
 }
 
 // Convert speed from km/h to mph
 float kmhToMph(float kmh) {
   return kmh * 0.621371;
 }
 
 // Convert speed from km/h to m/s
 float kmhToMs(float kmh) {
   return kmh / 3.6;
 }
 
 // Get compass direction from heading
 String getCompassDirection(float heading) {
   if (heading < 0) heading += 360;
   
   if (heading >= 337.5 || heading < 22.5) return "N";
   if (heading >= 22.5 && heading < 67.5) return "NE";
   if (heading >= 67.5 && heading < 112.5) return "E";
   if (heading >= 112.5 && heading < 157.5) return "SE";
   if (heading >= 157.5 && heading < 202.5) return "S";
   if (heading >= 202.5 && heading < 247.5) return "SW";
   if (heading >= 247.5 && heading < 292.5) return "W";
   if (heading >= 292.5 && heading < 337.5) return "NW";
   
   return "N";
 }
 
 // ==================== ADVANCED USAGE EXAMPLES ====================
 
 /*
 // EXAMPLE 1: Check if vehicle is moving
 void checkIfMoving() {
   if (gpsFixed) {
     if (currentSpeed > 5.0) {
       Serial.println("🚗 Vehicle is MOVING");
     } else {
       Serial.println("🅿️ Vehicle is STATIONARY");
     }
   }
 }
 
 // EXAMPLE 2: Geofencing (check if within area)
 bool isInsideGeofence(double centerLat, double centerLon, double radiusMeters) {
   if (!gpsFixed) return false;
   
   double distance = getDistance(currentLat, currentLon, centerLat, centerLon);
   return (distance <= radiusMeters);
 }
 
 // EXAMPLE 3: Speed limit alert
 void checkSpeedLimit() {
   const float SPEED_LIMIT = 60.0; // km/h
   
   if (gpsFixed && currentSpeed > SPEED_LIMIT) {
     Serial.println("⚠️ SPEEDING! Current: " + String(currentSpeed, 1) + " km/h");
   }
 }
 
 // EXAMPLE 4: Track total distance traveled
 float totalDistance = 0;
 double lastLat = 0, lastLon = 0;
 
 void updateTotalDistance() {
   if (gpsFixed && lastLat != 0) {
     double dist = getDistance(lastLat, lastLon, currentLat, currentLon);
     totalDistance += dist;
   }
   
   lastLat = currentLat;
   lastLon = currentLon;
 }
 
 // EXAMPLE 5: Navigation to destination
 void navigateToDestination(double destLat, double destLon) {
   if (!gpsFixed) return;
   
   double distance = getDistance(currentLat, currentLon, destLat, destLon);
   double bearing = getBearing(currentLat, currentLon, destLat, destLon);
   
   Serial.println("📍 Navigation:");
   Serial.println("   Distance: " + String(distance, 0) + " meters");
   Serial.println("   Direction: " + getCompassDirection(bearing) + " (" + String(bearing, 1) + "°)");
 }
 */