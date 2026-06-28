#include "safety.h"
#include "config.h"

static uint32_t s_lastValidTempMs = 0;

void safetyInit() { s_lastValidTempMs = millis(); }

void safetyNoteValidTemp(uint32_t nowMs) { s_lastValidTempMs = nowMs; }

FaultCode safetyEvaluate(const SystemState& st, uint32_t nowMs) {
  // 1) Sensor gagal: tak ada pembacaan valid dalam batas waktu.
  if ((uint32_t)(nowMs - s_lastValidTempMs) > SENSOR_TIMEOUT_MS) return FLT_SENSOR;
  // 2) Over-temp.
  if (st.suhu > SAFE_MAX_TEMP) return FLT_OVERTEMP;
  return FLT_NONE;
}

RpmStatus safetyRpmStatus(const SystemState& st, uint32_t nowMs) {
  if (st.opState != ST_RUNNING) return RPMS_STARTUP;
  if ((uint32_t)(nowMs - st.runStartMs) < RPM_STARTUP_MS) return RPMS_STARTUP;
  float r = st.rpm;
  if (r > RPM_WARN_HIGH) return RPMS_ERR_HIGH;
  if (r > RPM_NORM_HIGH) return RPMS_WARN_HIGH;
  if (r >= RPM_NORM_LOW) return RPMS_NORMAL;
  if (r >= RPM_WARN_LOW) return RPMS_WARN_LOW;
  return RPMS_ERR_LOW;
}
