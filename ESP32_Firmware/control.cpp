#include "control.h"
#include "config.h"
#include <math.h>

// FIS engine header-only — di-include HANYA di translation unit ini.
#include "Fis_Header.h"

// Metode diverifikasi via simulasi closed-loop (lihat tools/control_sim.py):
//   - Plant blower non-monoton, puncak panas ~25%, batas panas/dingin ~30%.
//   - Kontrol beroperasi di sisi monoton [CTRL_BLOWER_MIN..DIMMER_MAX] = [25..85].
//   - FoPID dihitung dalam °C (intuitif); koreksi DIKURANGI dari FIS karena
//     blower besar = mendinginkan (BLOWER_IS_COOLER): saat dingin (e>0) → blower
//     turun mendekati 25 (memanaskan); saat overshoot (e<0) → blower naik (dingin).

static float s_integral = 0.0f;
static float s_prevErrC = 0.0f;

void controlReset(const SystemState& st) {
  s_integral = 0.0f;
  s_prevErrC = st.setPoint - st.suhu;
}

void controlCompute(SystemState& st) {
  float errC  = st.setPoint - st.suhu;              // °C (+ = terlalu dingin)
  float dErrC = errC - s_prevErrC;  s_prevErrC = errC;

  // ── FoPID (fractional, dalam °C) ───────────────────────────────────────────
  s_integral += powf(DT_FIXED, st.lambda) * errC;
  s_integral  = constrain(s_integral, -FOPID_I_CLAMP, FOPID_I_CLAMP);  // anti-windup
  float deriv = powf(DT_FIXED, -st.mu) * dErrC;
  deriv = constrain(deriv, -FOPID_D_CLAMP, FOPID_D_CLAMP);
  float u = st.Kp * errC + st.Ki * s_integral + st.Kd * deriv;
  u = constrain(u, -FOPID_U_CLAMP, FOPID_U_CLAMP);

  // ── Fuzzy (peta error → blower, asimetris panas/dingin) ────────────────────
  float fisIn1 = constrain(errC, FIS_ERR_MIN, FIS_ERR_MAX);
  float fisIn2 = constrain((dErrC + 5.0f) / 10.0f * 5.0f, 0.0f, 5.0f);
  float fis = fuzzy_blower(fisIn1, fisIn2);

  // ── Gabung + clamp ke rentang kerja monoton [25..85] ───────────────────────
  float corr = (BLOWER_IS_COOLER ? -1.0f : 1.0f) * (u * st.beta);
  float raw  = fis + corr;
  raw = constrain(raw, (float)CTRL_BLOWER_MIN, (float)DIMMER_MAX);

  st.error      = errC;
  st.dError     = dErrC;
  st.integral   = s_integral;
  st.derivative = deriv;
  st.uFopid     = u;
  st.fisOut     = fis;
  st.blowerPct  = (int)lroundf(raw);
}
