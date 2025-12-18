/*
 * SIMPLE SIM A7670E TEST CODE
 * 
 * This code just powers on the modem and lets you send AT commands
 * via Serial Monitor
 AT                    → Check if modem responds
AT+CPIN?              → Check SIM card
AT+CREG?              → Check network registration
AT+CSQ                → Check signal strength
AT+CGMI               → Get manufacturer info
AT+CGMM               → Get model
ATI                   → Get modem info
send sms
 */

 #define MODEM_RXD           5
 #define MODEM_TXD           4
 #define MODEM_PWR_PIN      15
 #define MODEM_RESET_PIN    19
 
 HardwareSerial modemSerial(1);
 
 void setup() {
   // USB Serial for commands
   Serial.begin(115200);
   delay(2000);
   
   Serial.println("\n=== SIM A7670E TEST ===\n");
   
   // Modem pins
   pinMode(MODEM_PWR_PIN, OUTPUT);
   pinMode(MODEM_RESET_PIN, OUTPUT);
   
   digitalWrite(MODEM_RESET_PIN, HIGH);
   digitalWrite(MODEM_PWR_PIN, LOW);
   
   // Start modem serial
   modemSerial.begin(115200, SERIAL_8N1, MODEM_RXD, MODEM_TXD);
   
   Serial.println("Powering on modem...");
   digitalWrite(MODEM_PWR_PIN, HIGH);
   delay(1000);
   digitalWrite(MODEM_PWR_PIN, LOW);
   delay(3000);
   
   Serial.println("OK Ready!");
   Serial.println("\nType AT commands (e.g., AT, AT+CPIN?, AT+CREG?)");
   Serial.println("================================================\n");
    //send sms
    modemSerial.println("AT+CMGF=1");
    delay(1000);
    modemSerial.println("AT+CMGS=\"+250792957781\"");
    delay(1000);
    modemSerial.println("Hello, this is a test SMS");
    delay(1000);
    modemSerial.println((char)26);
    delay(1000);
    Serial.println("SMS sent");
 }
 
 void loop() {
   // Send from Serial Monitor to Modem
   if (Serial.available()) {
     String cmd = Serial.readStringUntil('\n');
     cmd.trim();
     modemSerial.println(cmd);
     Serial.println("> " + cmd);
   }
   
   // Send from Modem to Serial Monitor
   if (modemSerial.available()) {
     String response = modemSerial.readString();
     Serial.print(response);
   }
  
 }