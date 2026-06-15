#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  Serial.println("Firmware iniciando");  
}
void loop() {
  Serial.println("Firmware funcionando");
  delay(2000);
}
