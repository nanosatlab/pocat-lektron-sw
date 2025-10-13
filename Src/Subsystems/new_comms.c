#include "radiolib_wrapper.h"
#include "new_comms.h"

void new_comms_task(void *pv_parameters)
{

}

void new_setup_comms(void)
{
    // Apply the default configuration
}

static void new_process_comms(void)
{

}


// #include <RadioLib.h>

// SX1262 radio = new Module(10, 2, 3, 9);

// void setup() {
//   radio.begin();
// }

// void loop() {
//   // Perform CAD
//   int result = radio.scanChannel();

//   if (result == RADIOLIB_LORA_DETECTED) {
//     // "Activity detected, switching to RX...";
//     radio.startReceive();   // Enter receive mode
//   } else {
//     // "No activity";
//   }

//   delay(5000);  // Sleep/duty cycle
// }
