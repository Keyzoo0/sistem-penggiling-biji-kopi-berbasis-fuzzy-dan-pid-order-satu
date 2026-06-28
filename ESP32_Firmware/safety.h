// =============================================================================
//  safety.h — Supervisor guard SUHU + status RPM (warning). Lihat ARCHITECTURE §8
// =============================================================================
#ifndef SAFETY_H
#define SAFETY_H

#include "types.h"

void      safetyInit();
void      safetyNoteValidTemp(uint32_t nowMs);                 // panggil saat suhu valid
FaultCode safetyEvaluate(const SystemState& st, uint32_t nowMs); // FLT_NONE bila aman
RpmStatus safetyRpmStatus(const SystemState& st, uint32_t nowMs);

#endif // SAFETY_H
