#include "radioLibWrapper.h"

SX1262 radio = new Module(1, 2, 3, 4); // TODO: change pins to actual ones (these are made up)

void comms_task(void *pv_parameters)
{

}

void setup_comms(void)
{
    // Apply the default configuration
}

static void process_comms(void)
{

}


#include <RadioLib.h>

SX1262 radio = new Module(10, 2, 3, 9);

void setup() {
  radio.begin();
}

void loop() {
  // Perform CAD
  int result = radio.scanChannel();

  if (result == RADIOLIB_LORA_DETECTED) {
    Serial.println("Activity detected, switching to RX...");
    radio.startReceive();   // Enter receive mode
  } else {
    Serial.println("No activity.");
  }

  delay(5000);  // Sleep/duty cycle
}
