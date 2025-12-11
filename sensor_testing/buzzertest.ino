#define BUZZER_PIN 25   // Buzzer pin

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  Serial.begin(115200);
}

void loop() {
  digitalWrite(BUZZER_PIN, HIGH);
  Serial.println("Buzzer on");
  delay(1000);
  digitalWrite(BUZZER_PIN, LOW);  
  Serial.println("Buzzer off");
  delay(1000);
}