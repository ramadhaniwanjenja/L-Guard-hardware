/*
 * SIM A7670E COMPLETE TEST SUITE
 * Tests: SIM, Network, SMS, GPRS, HTTP, HTTPS
 */

 #define MODEM_RXD 5
 #define MODEM_TXD 4
 #define MODEM_PWR_PIN 15
 #define MODEM_DTR_PIN 18
 #define MODEM_RESET_PIN 19
 
 HardwareSerial modemSerial(1);
 
 // Configuration
 String APN = "internet.mtn.rw";
 String PHONE_NUMBER = "+250792957781";  // Your number
 String DEVICE_ID = "RWANDA_TEST_001";
 
 void setup() {
   Serial.begin(115200);
   delay(3000);
   
   Serial.println("\n╔════════════════════════════════════════╗");
   Serial.println("║     SIM A7670E COMPLETE TEST SUITE     ║");
   Serial.println("╚════════════════════════════════════════╝\n");
   
   // Initialize all pins
   pinMode(MODEM_PWR_PIN, OUTPUT);
   pinMode(MODEM_DTR_PIN, OUTPUT);
   pinMode(MODEM_RESET_PIN, OUTPUT);
   
   digitalWrite(MODEM_PWR_PIN, LOW);
   digitalWrite(MODEM_DTR_PIN, LOW);
   digitalWrite(MODEM_RESET_PIN, HIGH);
   
   // Start modem serial
   modemSerial.begin(115200, SERIAL_8N1, MODEM_RXD, MODEM_TXD);
   delay(2000);
   
   // Clear buffer
   while(modemSerial.available()) modemSerial.read();
   
   // Run complete test
   runCompleteTest();
 }
 
 void runCompleteTest() {
   Serial.println("════════════════════════════════════════");
   Serial.println("          STEP 1: POWER ON");
   Serial.println("════════════════════════════════════════");
   
   // Power on sequence
   Serial.println("Powering modem ON...");
   digitalWrite(MODEM_PWR_PIN, HIGH);
   delay(1500);
   digitalWrite(MODEM_PWR_PIN, LOW);
   delay(8000);
   
   Serial.println("\n════════════════════════════════════════");
   Serial.println("       STEP 2: BASIC COMMUNICATION");
   Serial.println("════════════════════════════════════════");
   
   if (!testAT()) {
     Serial.println("❌ FAIL: Modem not responding!");
     return;
   }
   
   Serial.println("\n════════════════════════════════════════");
   Serial.println("          STEP 3: SIM CARD");
   Serial.println("════════════════════════════════════════");
   
   if (!testSIM()) {
     Serial.println("❌ FAIL: SIM card issue!");
     return;
   }
   
   Serial.println("\n════════════════════════════════════════");
   Serial.println("       STEP 4: NETWORK REGISTRATION");
   Serial.println("════════════════════════════════════════");
   
   if (!testNetwork()) {
     Serial.println("❌ FAIL: Network registration issue!");
     return;
   }
   
   Serial.println("\n════════════════════════════════════════");
   Serial.println("          STEP 5: SMS TEST");
   Serial.println("════════════════════════════════════════");
   
   testSMS();
   
   Serial.println("\n════════════════════════════════════════");
   Serial.println("        STEP 6: GPRS CONNECTION");
   Serial.println("════════════════════════════════════════");
   
   if (!testGPRS()) {
     Serial.println("❌ FAIL: GPRS connection issue!");
     return;
   }
   
   Serial.println("\n════════════════════════════════════════");
   Serial.println("        STEP 7: HTTP TEST (NO SSL)");
   Serial.println("════════════════════════════════════════");
   
   testHTTP();
   
   Serial.println("\n════════════════════════════════════════");
   Serial.println("       STEP 8: HTTPS TEST (MAIN GOAL)");
   Serial.println("════════════════════════════════════════");
   
   testHTTPS();
   
   Serial.println("\n════════════════════════════════════════");
   Serial.println("         FINAL: YOUR API TEST");
   Serial.println("════════════════════════════════════════");
   
   testYourAPI();
   
   Serial.println("\n╔════════════════════════════════════════╗");
   Serial.println("║           TEST COMPLETE!              ║");
   Serial.println("╚════════════════════════════════════════╝\n");
 }
 
 // ==================== TEST FUNCTIONS ====================
 
 bool testAT() {
   Serial.println("Testing AT command...");
   
   modemSerial.println("AT");
   delay(1000);
   
   String response = readResponse(2000);
   Serial.print("Response: ");
   Serial.println(response);
   
   if (response.indexOf("OK") >= 0) {
     Serial.println("✅ AT OK");
     
     // Turn echo off
     modemSerial.println("ATE0");
     delay(500);
     readResponse(1000);
     
     // Enable verbose errors
     modemSerial.println("AT+CMEE=2");
     delay(500);
     readResponse(1000);
     
     return true;
   }
   
   return false;
 }
 
 bool testSIM() {
   Serial.println("Checking SIM card...");
   
   modemSerial.println("AT+CPIN?");
   delay(2000);
   
   String response = readResponse(3000);
   Serial.print("Response: ");
   Serial.println(response);
   
   if (response.indexOf("READY") >= 0) {
     Serial.println("✅ SIM READY");
     return true;
   } else if (response.indexOf("SIM PIN") >= 0) {
     Serial.println("⚠️ SIM requires PIN");
     // Uncomment if SIM has PIN:
     // modemSerial.println("AT+CPIN=\"1234\"");
     return true;
   }
   
   return false;
 }
 
 bool testNetwork() {
   Serial.println("Checking network signal...");
   
   modemSerial.println("AT+CSQ");
   delay(1000);
   String csq = readResponse(2000);
   Serial.print("Signal: ");
   Serial.println(csq);
   
   // Parse signal strength (first number after +CSQ:)
   if (csq.indexOf("+CSQ:") >= 0) {
     int start = csq.indexOf("+CSQ:") + 6;
     int end = csq.indexOf(",", start);
     String rssiStr = csq.substring(start, end);
     int rssi = rssiStr.toInt();
     
     Serial.print("RSSI: ");
     Serial.print(rssi);
     Serial.print(" (");
     
     if (rssi == 99) Serial.println("No signal)");
     else if (rssi >= 31) Serial.println("Excellent)");
     else if (rssi >= 26) Serial.println("Very Good)");
     else if (rssi >= 20) Serial.println("Good)");
     else if (rssi >= 10) Serial.println("Marginal)");
     else Serial.println("Poor)");
     
     if (rssi == 99) return false;
   }
   
   Serial.println("\nChecking network registration...");
   modemSerial.println("AT+CREG?");
   delay(1000);
   String creg = readResponse(2000);
   Serial.print("Registration: ");
   Serial.println(creg);
   
   if (creg.indexOf("0,1") >= 0 || creg.indexOf("0,5") >= 0) {
     Serial.println("✅ Registered on network");
     return true;
   }
   
   return false;
 }
 
 void testSMS() {
   Serial.println("Testing SMS functionality...");
   
   // Set SMS mode
   modemSerial.println("AT+CMGF=1");
   delay(1000);
   readResponse(1000);
   
   Serial.println("SMS ready (not sending to save credit)");
   Serial.println("To send SMS, uncomment code below");
   
   /*
   // Uncomment to actually send SMS:
   Serial.println("Sending test SMS...");
   modemSerial.print("AT+CMGS=\"");
   modemSerial.print(PHONE_NUMBER);
   modemSerial.println("\"");
   delay(2000);
   
   modemSerial.print("Test from SIM A7670E in Rwanda");
   delay(100);
   modemSerial.write(26); // Ctrl+Z
   delay(5000);
   
   String resp = readResponse(10000);
   if (resp.indexOf("+CMGS:") >= 0) {
     Serial.println("✅ SMS sent successfully!");
   } else {
     Serial.println("⚠️ SMS may have failed");
   }
   */
 }
 
 bool testGPRS() {
   Serial.println("Setting up GPRS...");
   
   // Set APN
   modemSerial.println("AT+CGDCONT=1,\"IP\",\"" + APN + "\"");
   delay(2000);
   readResponse(2000);
   
   // Activate PDP context
   Serial.println("Activating data connection...");
   modemSerial.println("AT+CGACT=1,1");
   delay(10000);
   
   String resp = readResponse(15000);
   Serial.print("Activation: ");
   Serial.println(resp);
   
   if (resp.indexOf("OK") < 0) {
     return false;
   }
   
   // Get IP address
   modemSerial.println("AT+CGPADDR=1");
   delay(2000);
   String ipResp = readResponse(3000);
   Serial.print("IP Address: ");
   Serial.println(ipResp);
   
   if (ipResp.indexOf("CGPADDR: 1") >= 0) {
     Serial.println("✅ GPRS connected with IP");
     return true;
   }
   
   return false;
 }
 
 void testHTTP() {
   Serial.println("Testing HTTP (no SSL)...");
   
   // Initialize HTTP
   modemSerial.println("AT+HTTPINIT");
   delay(3000);
   readResponse(2000);
   
   // Force HTTP mode (no SSL)
   modemSerial.println("AT+HTTPSSL=0");
   delay(1000);
   readResponse(1000);
   
   // Set URL to httpbin (HTTP, not HTTPS)
   modemSerial.println("AT+HTTPPARA=\"URL\",\"http://httpbin.org/get\"");
   delay(1000);
   readResponse(1000);
   
   // Execute GET
   Serial.println("Executing HTTP GET...");
   modemSerial.println("AT+HTTPACTION=0");
   delay(10000);
   
   String resp = readResponse(15000);
   Serial.print("HTTP Result: ");
   Serial.println(resp);
   
   if (resp.indexOf("+HTTPACTION: 0,200") >= 0) {
     Serial.println("✅ HTTP SUCCESS!");
     
     // Read response
     modemSerial.println("AT+HTTPREAD");
     delay(3000);
     readResponse(5000);
   }
   
   // Terminate
   modemSerial.println("AT+HTTPTERM");
   delay(1000);
   readResponse(1000);
 }
 
 void testHTTPS() {
   Serial.println("Testing HTTPS (SSL)...");
   Serial.println("This is the MAIN GOAL!");
   
   // Initialize HTTP
   modemSerial.println("AT+HTTPINIT");
   delay(3000);
   readResponse(2000);
   
   // Enable SSL
   modemSerial.println("AT+HTTPSSL=1");
   delay(1000);
   readResponse(1000);
   
   // Configure SSL
   modemSerial.println("AT+CSSLCFG=\"sslversion\",0,3");  // TLS 1.2
   delay(1000);
   readResponse(1000);
   
   // Set URL to https (not http)
   modemSerial.println("AT+HTTPPARA=\"URL\",\"https://httpbin.org/get\"");
   delay(1000);
   readResponse(1000);
   
   // Execute GET
   Serial.println("Executing HTTPS GET...");
   modemSerial.println("AT+HTTPACTION=0");
   delay(15000);
   
   String resp = readResponse(20000);
   Serial.print("HTTPS Result: ");
   Serial.println(resp);
   
   if (resp.indexOf("+HTTPACTION: 0,200") >= 0) {
     Serial.println("🎉🎉🎉 HTTPS SUCCESS! 🎉🎉🎉");
     Serial.println("✅ Your SIM card supports HTTPS!");
     Serial.println("✅ Can connect to secure APIs!");
     
     // Read response
     modemSerial.println("AT+HTTPREAD");
     delay(3000);
     readResponse(5000);
     
   } else if (resp.indexOf("714") >= 0 || resp.indexOf("702") >= 0) {
     Serial.println("❌ SSL/TLS Error");
     Serial.println("Modem firmware may not support HTTPS");
   }
   
   // Terminate
   modemSerial.println("AT+HTTPTERM");
   delay(1000);
   readResponse(1000);
 }
 
 void testYourAPI() {
   Serial.println("Testing YOUR API endpoint...");
   
   // Initialize
   modemSerial.println("AT+HTTPINIT");
   delay(3000);
   readResponse(2000);
   
   // Try HTTPS first
   modemSerial.println("AT+HTTPSSL=1");
   delay(1000);
   readResponse(1000);
   
   modemSerial.println("AT+CSSLCFG=\"sslversion\",0,3");
   delay(1000);
   readResponse(1000);
   
   // Your API URL
   String apiUrl = "https://lguard-backend-service.onrender.com/api/v1/telemetry/ingest";
   modemSerial.println("AT+HTTPPARA=\"URL\",\"" + apiUrl + "\"");
   delay(1000);
   readResponse(1000);
   
   modemSerial.println("AT+HTTPPARA=\"CONTENT\",\"application/json\"");
   delay(1000);
   readResponse(1000);
   
   // Simple test data
   String testData = "{\"deviceId\":\"" + DEVICE_ID + "\",\"test\":true,\"message\":\"Testing from Rwanda\"}";
   
   modemSerial.println("AT+HTTPDATA=" + String(testData.length()) + ",20000");
   delay(2000);
   readResponse(2000);
   
   modemSerial.println(testData);
   delay(2000);
   
   modemSerial.write(26); // Ctrl+Z
   delay(3000);
   readResponse(3000);
   
   Serial.println("POSTing to your API...");
   modemSerial.println("AT+HTTPACTION=1");
   delay(20000);
   
   String resp = readResponse(30000);
   Serial.print("API Result: ");
   Serial.println(resp);
   
   if (resp.indexOf("+HTTPACTION: 1,200") >= 0 || resp.indexOf("+HTTPACTION: 1,201") >= 0) {
     Serial.println("🎉🎉🎉 YOUR API SUCCESS! 🎉🎉🎉");
     Serial.println("✅ Data sent to your API!");
     Serial.println("✅ System is WORKING!");
     
     // Read response
     modemSerial.println("AT+HTTPREAD");
     delay(3000);
     readResponse(5000);
   }
   
   // Terminate
   modemSerial.println("AT+HTTPTERM");
   delay(1000);
   readResponse(1000);
 }
 
 String readResponse(unsigned long timeout) {
   String response = "";
   unsigned long start = millis();
   
   while (millis() - start < timeout) {
     while (modemSerial.available()) {
       char c = modemSerial.read();
       response += c;
     }
     
     if (response.length() > 0 && millis() - start > 500) {
       // Check if we have complete response
       if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) {
         delay(100);
         // Get any remaining
         while(modemSerial.available()) {
           response += (char)modemSerial.read();
         }
         break;
       }
     }
   }
   
   // Clean up
   response.trim();
   return response;
 }
 
 void loop() {
   // Nothing here
   delay(1000);
 }