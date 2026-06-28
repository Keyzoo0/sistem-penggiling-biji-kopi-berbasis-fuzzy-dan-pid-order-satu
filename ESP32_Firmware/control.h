// =============================================================================
//  control.h — Hybrid Fuzzy (FIS) + Fractional-Order PID
//  Dipanggil hanya dari realtime task saat RUNNING+FUZZY.
// =============================================================================
#ifndef CONTROL_H
#define CONTROL_H

#include "types.h"

void controlReset(const SystemState& st);   // nol-kan integral & prev-error
void controlCompute(SystemState& st);        // isi error,dError,uFopid,...,blowerPct

#endif // CONTROL_H
