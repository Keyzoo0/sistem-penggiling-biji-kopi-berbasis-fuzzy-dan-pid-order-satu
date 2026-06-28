#include "control.h"
#include "config.h"
#include <math.h>

// FIS engine header-only — di-include HANYA di translation unit ini.
#include "Fis_Header.h"

// Metode v2 (diverifikasi via tools/control_sim.py):
//   - Kurva FIS: jauh<SP→30% · dekat SP→20% · di atas SP→60..100%.
//   - Deadband ±SP_DEADBAND_PCT → blower = DEADBAND_BLOWER (default 0 = mati).
//   - FoPID (°C) = trim lembut di atas kurva (tunable Kp/Ki/Kd dari web), aktif
//     hanya di luar deadband; koreksi DIKURANGI dari FIS (BLOWER_IS_COOLER).

static float s_integral = 0.0f;
static float s_prevErrC = 0.0f;

void controlReset(const SystemState& st) {
  s_integral = 0.0f;
  s_prevErrC = st.setPoint - st.suhu;
}

void controlCompute(SystemState& st) {
  float errC  = st.setPoint - st.suhu;              // °C (+ = di bawah SP)
  float dErrC = errC - s_prevErrC;  s_prevErrC = errC;
  float band  = (SP_DEADBAND_PCT / 100.0f) * st.setPoint;

  // ── Deadband: setpoint tercapai → blower mati (atau hold) ──────────────────
  if (fabsf(errC) <= band) {
    s_integral   = 0.0f;                            // cegah windup di deadband
    st.error     = errC;  st.dError = dErrC;
    st.integral  = 0.0f;  st.derivative = 0.0f;  st.uFopid = 0.0f;
    st.fisOut    = (float)DEADBAND_BLOWER;
    st.blowerPct = DEADBAND_BLOWER;
    return;
  }

  // ── FoPID trim (fractional, °C) ────────────────────────────────────────────
  s_integral += powf(DT_FIXED, st.lambda) * errC;
  s_integral  = constrain(s_integral, -FOPID_I_CLAMP, FOPID_I_CLAMP);
  float deriv = powf(DT_FIXED, -st.mu) * dErrC;
  deriv = constrain(deriv, -FOPID_D_CLAMP, FOPID_D_CLAMP);
  float u = st.Kp * errC + st.Ki * s_integral + st.Kd * deriv;
  u = constrain(u, -FOPID_U_CLAMP, FOPID_U_CLAMP);

  // ── Kurva fuzzy + trim ─────────────────────────────────────────────────────
  float fisIn1 = constrain(errC, FIS_ERR_MIN, FIS_ERR_MAX);
  float fisIn2 = constrain((dErrC + 5.0f) / 10.0f * 5.0f, 0.0f, 5.0f);
  float fis = fuzzy_blower(fisIn1, fisIn2);

  float corr = (BLOWER_IS_COOLER ? -1.0f : 1.0f) * (u * st.beta);
  float raw  = constrain(fis + corr, 0.0f, (float)DIMMER_MAX);

  st.error      = errC;
  st.dError     = dErrC;
  st.integral   = s_integral;
  st.derivative = deriv;
  st.uFopid     = u;
  st.fisOut     = fis;
  st.blowerPct  = (int)lroundf(raw);
}
