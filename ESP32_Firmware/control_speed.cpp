#include "control_speed.h"
#include "config.h"
#include <Arduino.h>

// PID kecepatan klasik (direct-acting): RPM kurang → freq naik.
static float s_integ = 0.0f;
static float s_prevE = 0.0f;

void controlSpeedReset(const SystemState& st) {
  s_integ = 0.0f;
  s_prevE = st.speedSP - st.rpm;
}

void controlSpeedCompute(SystemState& st) {
  float e  = st.speedSP - st.rpm;                 // error RPM
  s_integ += e * DT_FIXED;
  s_integ  = constrain(s_integ, -300.0f, 300.0f); // anti-windup
  float de = (e - s_prevE) / DT_FIXED;  s_prevE = e;

  float u = st.sKp * e + st.sKi * s_integ + st.sKd * de;
  st.vfdFreq = constrain(u, 0.0f, VFD_FREQ_MAX_HZ);
}
