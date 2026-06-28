// =============================================================================
//  logging.h — SD CSV. Buka/tutup terikat transisi RUNNING. Akses dijaga SD mutex.
// =============================================================================
#ifndef LOGGING_H
#define LOGGING_H

#include "types.h"

void   loggingInit();
bool   loggingStart(SystemState& st);   // buat file unik + header; set st.logFile/logging
void   loggingStop(SystemState& st);
void   loggingAppend(const SystemState& st);
String loggingListJson();               // ["log_fuzzy_001.csv", ...]
bool   loggingFileExists(const String& path);
bool   loggingReady();

#endif // LOGGING_H
