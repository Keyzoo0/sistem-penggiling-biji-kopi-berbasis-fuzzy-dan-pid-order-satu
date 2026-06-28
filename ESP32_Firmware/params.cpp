#include "params.h"
#include "config.h"
#include <Preferences.h>

static Preferences prefs;
#define PARAMS_NS "kopiprm"

void paramsLoad(SystemState& st) {
  prefs.begin(PARAMS_NS, true);                 // read-only
  st.Kp         = prefs.getFloat("kp",   DEF_KP);
  st.Ki         = prefs.getFloat("ki",   DEF_KI);
  st.Kd         = prefs.getFloat("kd",   DEF_KD);
  st.lambda     = prefs.getFloat("lam",  DEF_LAMBDA);
  st.mu         = prefs.getFloat("mu",   DEF_MU);
  st.beta       = prefs.getFloat("beta", DEF_BETA);
  st.setPoint   = prefs.getFloat("sp",   DEF_SETPOINT);
  st.durationMin= prefs.getULong("dur",  DEF_DURATION_MIN);
  st.servoDeg   = prefs.getInt(  "servo", SERVO_MAX);
  st.freqMotor  = prefs.getFloat("freq", DEF_FREQ_MOTOR);
  prefs.end();
  Serial.printf("[PARAMS] load: Kp=%.3f Ki=%.3f Kd=%.3f SP=%.1f dur=%lu servo=%d freq=%.1f\n",
                st.Kp, st.Ki, st.Kd, st.setPoint, (unsigned long)st.durationMin, st.servoDeg, st.freqMotor);
}

void paramsSave(const SystemState& st) {
  prefs.begin(PARAMS_NS, false);
  prefs.putFloat("kp",   st.Kp);
  prefs.putFloat("ki",   st.Ki);
  prefs.putFloat("kd",   st.Kd);
  prefs.putFloat("lam",  st.lambda);
  prefs.putFloat("mu",   st.mu);
  prefs.putFloat("beta", st.beta);
  prefs.putFloat("sp",   st.setPoint);
  prefs.putULong("dur",  st.durationMin);
  prefs.putInt(  "servo", st.servoDeg);
  prefs.putFloat("freq", st.freqMotor);
  prefs.end();
}
