#define BUZZER_PIN 12   // Buzzer pin

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
}

void loop() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(1000);
  digitalWrite(BUZZER_PIN, LOW);
  delay(1000);
}