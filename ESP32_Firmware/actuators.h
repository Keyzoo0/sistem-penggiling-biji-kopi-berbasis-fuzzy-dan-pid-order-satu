// =============================================================================
//  actuators.h — dimmer(blower) · servo(gas) · LED indikator
//  Dipanggil HANYA dari realtime task (single-writer) → tanpa mutex internal.
// =============================================================================
#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <Arduino.h>

void actuatorsInit();
void actuatorSetBlower(int pct);                 // clamp [DIMMER_MIN..DIMMER_MAX]
int  actuatorGetBlower();
void actuatorSetServo(int deg);                  // persist ditangani params.*
int  actuatorGetServo();
void actuatorLed(uint8_t opState);               // pola LED per operating state
void actuatorsSafeState();                        // gas tutup (0°) + blower 0%

#endif // ACTUATORS_H
