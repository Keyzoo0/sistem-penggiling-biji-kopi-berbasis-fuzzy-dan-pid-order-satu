// =============================================================================
//  vfd.h — Kontrol VFD MCU-T13 via Modbus RTU (Serial1 + MAX485).
//  Lihat VFD_Modbus_ESP32_MAX485.md. CRC16 diverifikasi cocok dokumen.
//  Dipanggil hanya dari realtime/control task (single-caller).
// =============================================================================
#ifndef VFD_H
#define VFD_H

#include <Arduino.h>

void vfdInit();
bool vfdSetFrequency(float hz);          // 0..VFD_FREQ_MAX_HZ
bool vfdRun();                            // run forward
bool vfdStop();
bool vfdReadStatus(uint16_t* outValue);  // baca status word (opsional)

#endif // VFD_H
