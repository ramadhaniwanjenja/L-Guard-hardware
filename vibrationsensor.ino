#define VIBRATION_SENSOR_PIN 32 
void setup() {
  pinMode(VIBRATION_SENSOR_PIN, INPUT);
}

void loop() {
  int vibrationValue = analogRead(VIBRATION_SENSOR_PIN);
  Serial.println(vibrationValue);
  delay(100);
}