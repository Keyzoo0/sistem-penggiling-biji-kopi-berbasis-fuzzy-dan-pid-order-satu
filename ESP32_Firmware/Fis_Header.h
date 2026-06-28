// =============================================================================
//  Fis_Header.h — Fuzzy Inference System blower (KURVA v2, diverifikasi sim)
//
//  KURVA (spec pemilik alat, relatif setpoint):
//    jauh di bawah SP  → 30%  (memanaskan lembut)
//    mendekati SP      → 20%  (memanaskan kuat; "kecil = panas")
//    dalam ±1% SP      →  0%  (deadband — ditangani override di control.cpp)
//    di atas SP        → 60..100% (mendinginkan; makin jauh makin besar)
//
//  4 MF error × 3 MF Δerror → centroid. Output di-MF-kan ke [10..108]%.
//  Deadband 0% TIDAK di sini (dipaksa di control.cpp agar tepat 0).
//  Verifikasi: tools/control_sim.py → rise ~1.4 mnt, overshoot ~0%, osilasi ~0.5%.
// =============================================================================
#ifndef FIS_HEADER_H
#define FIS_HEADER_H

#include <math.h>

#define FIS_N_IN1   4
#define FIS_N_IN2   3
#define FIS_N_RULES 12

static inline float fis_trimf(float x, float a, float b, float c) {
  if (b == a && b == c) return (x == b) ? 1.0f : 0.0f;
  if (a == b) { if (x <= b) return 1.0f; return (x >= c) ? 0.0f : (c - x) / (c - b); }
  if (b == c) { if (x >= b) return 1.0f; return (x <= a) ? 0.0f : (x - a) / (b - a); }
  if (x <= a || x >= c) return 0.0f;
  if (x <= b) return (x - a) / (b - a);
  return (c - x) / (c - b);
}

// ─── MF Input1 (error °C = SP - T). Level 1..4: paling PANAS → paling DINGIN ─
static const float FIS_IN1_MF[FIS_N_IN1][3] = {
  { -45.0f, -30.0f,  -8.0f },   // 1 sangat_panas (overshoot besar)  → dingin maks
  { -18.0f,  -6.0f,   0.0f },   // 2 panas        (sedikit di atas SP)→ dingin
  {   0.0f,   6.0f,  18.0f },   // 3 hangat       (sedikit di bawah SP)→ panas (20%)
  {   8.0f,  30.0f,  55.0f }    // 4 sangat_dingin(jauh di bawah SP)  → panas (30%)
};

// ─── MF Input2 (Δerror dipetakan ke [0..5]) ─────────────────────────────────
static const float FIS_IN2_MF[FIS_N_IN2][3] = {
  { 0.0f,  0.0f,  2.08f },
  { 0.41f, 2.5f,  4.58f },
  { 2.91f, 5.0f,  5.0f  }
};

// ─── MF Output (blower %). Sejajar Input1 ──────────────────────────────────
static const float FIS_OUT_MF[FIS_N_IN1][3] = {
  { 90.0f, 100.0f, 108.0f },   // 1 → dingin maksimum
  { 48.0f,  60.0f,  72.0f },   // 2 → dingin (mulai di atas SP)
  { 14.0f,  20.0f,  26.0f },   // 3 → panas (mendekati SP)
  { 24.0f,  30.0f,  40.0f }    // 4 → panas lembut (jauh di bawah SP)
};

#define FIS_OUT_MIN  10.0f
#define FIS_OUT_MAX 108.0f
#define FIS_CENTROID_STEPS 120

// ─── Rule base — error-dominant: out level = input1 level (delta netral) ────
static const int FIS_RULES[FIS_N_RULES][3] = {
  {1,1,1}, {1,2,1}, {1,3,1},
  {2,1,2}, {2,2,2}, {2,3,2},
  {3,1,3}, {3,2,3}, {3,3,3},
  {4,1,4}, {4,2,4}, {4,3,4}
};

// ─── Evaluasi FIS: fuzzifikasi → inferensi (min) → defuzzifikasi centroid ────
static inline float fuzzy_blower(float e, float de) {
  float mu1[FIS_N_IN1], mu2[FIS_N_IN2];
  for (int i = 0; i < FIS_N_IN1; i++) mu1[i] = fis_trimf(e,  FIS_IN1_MF[i][0], FIS_IN1_MF[i][1], FIS_IN1_MF[i][2]);
  for (int j = 0; j < FIS_N_IN2; j++) mu2[j] = fis_trimf(de, FIS_IN2_MF[j][0], FIS_IN2_MF[j][1], FIS_IN2_MF[j][2]);

  float ruleStrength[FIS_N_RULES];
  for (int r = 0; r < FIS_N_RULES; r++)
    ruleStrength[r] = fminf(mu1[FIS_RULES[r][0] - 1], mu2[FIS_RULES[r][1] - 1]);

  float sumXMu = 0.0f, sumMu = 0.0f;
  const float step = (FIS_OUT_MAX - FIS_OUT_MIN) / (float)FIS_CENTROID_STEPS;
  for (int s = 0; s <= FIS_CENTROID_STEPS; s++) {
    float x = FIS_OUT_MIN + step * (float)s;
    float aggMu = 0.0f;
    for (int r = 0; r < FIS_N_RULES; r++) {
      if (ruleStrength[r] <= 0.0f) continue;
      int outIdx = FIS_RULES[r][2] - 1;
      float clipped = fminf(ruleStrength[r], fis_trimf(x, FIS_OUT_MF[outIdx][0], FIS_OUT_MF[outIdx][1], FIS_OUT_MF[outIdx][2]));
      if (clipped > aggMu) aggMu = clipped;
    }
    sumXMu += x * aggMu;
    sumMu  += aggMu;
  }
  if (sumMu <= 0.0001f) return 20.0f;   // fallback: panas ringan
  return sumXMu / sumMu;
}

#endif // FIS_HEADER_H
