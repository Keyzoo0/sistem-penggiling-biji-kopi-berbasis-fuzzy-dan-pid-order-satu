// =============================================================================
//  control_speed.h — PID kecepatan (Fadel): error RPM → frekuensi VFD.
// =============================================================================
#ifndef CONTROL_SPEED_H
#define CONTROL_SPEED_H

#include "types.h"

void controlSpeedReset(const SystemState& st);
void controlSpeedCompute(SystemState& st);   // isi st.vfdFreq

#endif // CONTROL_SPEED_H
