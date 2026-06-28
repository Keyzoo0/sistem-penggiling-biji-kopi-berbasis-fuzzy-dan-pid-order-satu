// =============================================================================
//  sensors.h — MLX90614 (suhu) · DS3231 (RTC) · encoder→RPM · PZEM (daya)
// =============================================================================
#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "RTClib.h"

void     sensorsInit();
float    sensorsReadTemp(bool& ok);     // I2C-locked; ok=false bila NaN/di luar rentang
void     sensorsServiceRPM(uint32_t dtMs, float& rpmDryerOut);
void     sensorsReadPzem(float& v, float& i, float& p, float& e, float& f, float& pfOut);

DateTime rtcNow();                       // I2C-locked
void     rtcSyncFromCompile();

#endif // SENSORS_H
