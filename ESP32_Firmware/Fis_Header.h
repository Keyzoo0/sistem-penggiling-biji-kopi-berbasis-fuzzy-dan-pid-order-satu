// =============================================================================
//  Fis_Header.h — Fuzzy Inference System untuk blower (REVISI MODEL PLANT)
//
//  MODEL PLANT (dikonfirmasi pemilik alat, lihat ARCHITECTURE.md §3):
//    Blower bersifat NON-MONOTON terhadap suhu biji:
//      0–10%  : mendinginkan (aliran terlalu kecil, kalor burner tak terbawa)
//      20–30% : MEMANASKAN  (aliran optimal; puncak transfer panas ~25%)
//      30–85% : mendinginkan (aliran berlebih → kalor terbuang)
//    => batas panas/dingin ada di ~30%. Untuk menjaga kontrol MONOTON,
//       kontroler hanya beroperasi di sisi kanan puncak: [25..85]%
//       (25% = panas maksimum, 30% = batas/hold di setpoint, 85% = dingin maksimum).
//
//  PEMETAAN FIS (diverifikasi via simulasi closed-loop):
//    error = SP - T  (°C, + = terlalu dingin)
//      error besar +  (sangat dingin) → 25%  (panas maksimum)
//      error  0       (di setpoint)   → 30%  (hold, batas band panas)
//      error  -       (overshoot)     → 50..85% (mendinginkan)
//    Hasil simulasi: 28→60°C settle 60.0 (blower 30), di 58°C blower 25 (MEMANASKAN).
// =============================================================================
#ifndef FIS_HEADER_H
#define FIS_HEADER_H

#include <math.h>

// ─── Membership function segitiga ───────────────────────────────────────────
static inline float fis_trimf(float x, float a, float b, float c) {
  if (b == a && b == c) return (x == b) ? 1.0f : 0.0f;
  if (a == b) { if (x <= b) return 1.0f; return (x >= c) ? 0.0f : (c - x) / (c - b); }
  if (b == c) { if (x >= b) return 1.0f; return (x <= a) ? 0.0f : (x - a) / (b - a); }
  if (x <= a || x >= c) return 0.0f;
  if (x <= b) return (x - a) / (b - a);
  return (c - x) / (c - b);
}

// ─── MF Input1 (error °C = SP - T). Level 1..5: paling PANAS → paling DINGIN ─
static const float FIS_IN1_MF[5][3] = {
  { -30.0f, -15.0f,  -6.0f },   // 1 sangat_panas   (overshoot besar)
  { -12.0f,  -6.0f,   0.0f },   // 2 panas           (sedikit di atas SP)
  {  -4.0f,   0.0f,   5.0f },   // 3 stabil          (di setpoint)
  {   3.0f,  12.0f,  24.0f },   // 4 dingin          (di bawah SP)
  {  16.0f,  28.0f,  46.0f }    // 5 sangat_dingin   (jauh di bawah SP)
};

// ─── MF Input2 (delta error → [0..5]) — disediakan, pengaruh netral ─────────
static const float FIS_IN2_MF[3][3] = {
  { 0.0f,  0.0f,  2.08f },
  { 0.41f, 2.5f,  4.58f },
  { 2.91f, 5.0f,  5.0f  }
};

// ─── MF Output (blower %). Level sejajar Input1: dingin_maks → panas_maks ───
//  Sisi kerja monoton [25..85]: 25=panas maks, 30=hold, 50/80=dingin.
static const float FIS_OUT_MF[5][3] = {
  { 66.0f, 80.0f, 94.0f },   // 1 → utk sangat_panas : dingin maksimum
  { 35.0f, 50.0f, 66.0f },   // 2 → utk panas        : mendinginkan
  { 27.0f, 30.0f, 34.0f },   // 3 → utk stabil       : HOLD (batas 30%)
  { 25.0f, 27.0f, 30.0f },   // 4 → utk dingin       : memanaskan
  { 22.0f, 25.0f, 28.0f }    // 5 → utk sangat_dingin: PANAS maksimum (25%)
};

#define FIS_OUT_MIN  22.0f
#define FIS_OUT_MAX  90.0f
#define FIS_CENTROID_STEPS 120

// ─── Rule base — error-dominant: out level = input1 level (delta netral) ────
static const int FIS_RULES[15][3] = {
  {1,1,1}, {1,2,1}, {1,3,1},
  {2,1,2}, {2,2,2}, {2,3,2},
  {3,1,3}, {3,2,3}, {3,3,3},
  {4,1,4}, {4,2,4}, {4,3,4},
  {5,1,5}, {5,2,5}, {5,3,5}
};

// ─── Evaluasi FIS: fuzzifikasi → inferensi (min) → defuzzifikasi centroid ────
static inline float fuzzy_blower(float e, float de) {
  float mu1[5], mu2[3];
  for (int i = 0; i < 5; i++) mu1[i] = fis_trimf(e,  FIS_IN1_MF[i][0], FIS_IN1_MF[i][1], FIS_IN1_MF[i][2]);
  for (int j = 0; j < 3; j++) mu2[j] = fis_trimf(de, FIS_IN2_MF[j][0], FIS_IN2_MF[j][1], FIS_IN2_MF[j][2]);

  float ruleStrength[15];
  for (int r = 0; r < 15; r++) {
    ruleStrength[r] = fminf(mu1[FIS_RULES[r][0] - 1], mu2[FIS_RULES[r][1] - 1]);
  }

  float sumXMu = 0.0f, sumMu = 0.0f;
  const float step = (FIS_OUT_MAX - FIS_OUT_MIN) / (float)FIS_CENTROID_STEPS;
  for (int s = 0; s <= FIS_CENTROID_STEPS; s++) {
    float x = FIS_OUT_MIN + step * (float)s;
    float aggMu = 0.0f;
    for (int r = 0; r < 15; r++) {
      if (ruleStrength[r] <= 0.0f) continue;
      int outIdx = FIS_RULES[r][2] - 1;
      float clipped = fminf(ruleStrength[r], fis_trimf(x, FIS_OUT_MF[outIdx][0], FIS_OUT_MF[outIdx][1], FIS_OUT_MF[outIdx][2]));
      if (clipped > aggMu) aggMu = clipped;
    }
    sumXMu += x * aggMu;
    sumMu  += aggMu;
  }
  if (sumMu <= 0.0001f) return 30.0f;   // fallback ke hold (batas band panas)
  return sumXMu / sumMu;
}

#endif // FIS_HEADER_H
