// =============================================================================
//  params.h — Persist parameter tunable ke NVS (Preferences) + load saat boot.
// =============================================================================
#ifndef PARAMS_H
#define PARAMS_H

#include "types.h"

void paramsLoad(SystemState& st);   // isi st dari NVS (default config bila kosong)
void paramsSave(const SystemState& st);

#endif // PARAMS_H
