#include <Arduino.h>

void setup()
{
    Serial.begin(115200);

    delay(2000);

    Serial.println();
    Serial.println("==============================");
    Serial.println("ECE455 LoRa Swarm Data Logger");
    Serial.println("RAK11310 Firmware");
    Serial.println("==============================");
}

void loop()
{
    Serial.println("System running...");

    delay(1000);
}