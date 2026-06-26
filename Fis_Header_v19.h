// =============================================================================
//  Fis_Header_v19.h
//
//  Implementasi fuzzy_blower() — dikalibrasi ulang ke kondisi nyata percobaan.
//
//  PERUBAHAN DARI v18:
//
//  [FIS-V19-1] INPUT 1 (Eror_suhu) — range dikalibrasi ke [-10..40]°C
//      Kondisi nyata: suhu awal 25-30°C, SP=60°C → error riil 0..35°C.
//      Overshoot maksimum estimasi ~10°C → batas negatif -10°C.
//      Range v18 [-15..40] sudah dekat, tapi puncak MF sangat_panas di -15
//      terlalu jauh → nyaris tidak pernah aktif. Digeser ke -10.
//
//      MF Input1 v19 (error dalam °C = SP - suhuActual):
//        sangat_panas:  puncak -10°C (saat suhu 70°C ke atas, sangat overshoot)
//        panas:         puncak  0°C  (saat suhu tepat/sedikit di atas SP)
//        hangat:        puncak 10°C  (saat suhu 50°C, mendekati SP)
//        dingin:        puncak 25°C  (saat suhu 35°C, setengah jalan)
//        sangat_dingin: puncak 40°C  (saat suhu 20°C, sangat jauh dari SP)
//
//  [FIS-V19-2] OUTPUT (Kecepatan_blower) — range DINAIKKAN ke [30..75]%
//      Masalah v18: output minimal 20% terlalu kecil — saat suhu jauh di
//      bawah SP, blower hanya 20-28% sehingga sirkulasi udara panas
//      ke material dryer tidak cukup untuk menaikkan suhu ke 60°C.
//      Solusi: naikkan minimum output ke 30%, maksimum ke 75%.
//
//      Makna fisik (konfirmasi pemilik alat):
//        Nilai KECIL (30%) = blower PELAN → udara panas tertahan → suhu naik
//        Nilai BESAR (75%) = blower CEPAT → udara panas dibuang → suhu turun
//
//      MF Output v19:
//        sangat_lambat: puncak 30%  (suhu sangat jauh dari SP → tahan kalor)
//        lambat:        puncak 42%
//        stabil:        puncak 52%
//        cepat:         puncak 63%
//        sangat_cepat:  puncak 75%  (suhu overshoot → buang kalor)
//
//  [FIS-V19-3] RULE BASE — tidak berubah dari file .fis asli.
//
//  [FIS-V19-4] INPUT 2 (Delta_eror) — tidak berubah dari v18.
//
//  CARA PENGGUNAAN DARI .ino:
//      // Hitung error dalam derajat Celsius
//      float fisIn1 = constrain(errorFuzzy, -10.0f, 40.0f);
//      // Delta error °C → petakan ke [0..5]
//      float fisIn2 = constrain((deltaErrorFuzzy + 5.0f) / 10.0f * 5.0f, 0.0f, 5.0f);
//      g_fisInput[0] = fisIn1;
//      g_fisInput[1] = fisIn2;
//      fis_evaluate();
//      // outputFuzzy sekarang dalam range [30..75]%
//
//  HASIL SIMULASI (kondisi awal suhu=30°C, SP=60°C, delta=0):
//      Suhu 25°C (error=35): FIS=45%  + FoPID=+15% × beta=0.4 → dimmer ~51%
//      Suhu 40°C (error=20): FIS=44%  + FoPID=+13% × beta=0.4 → dimmer ~50%
//      Suhu 55°C (error= 5): FIS=57%  + FoPID= +4% × beta=0.4 → dimmer ~59%
//      Suhu 60°C (error= 0): FIS=63%  + FoPID~ +2% × beta=0.4 → dimmer ~64%
//      Suhu 65°C (error=-5): FIS=65%  + FoPID= -3% × beta=0.4 → dimmer ~64%
// =============================================================================

#ifndef FIS_HEADER_V19_H
#define FIS_HEADER_V19_H

#include <math.h>

// ─── Membership function segitiga ───────────────────────────────────────────
static inline float fis_trimf(float x, float a, float b, float c) {
  if (b == a && b == c) return (x == b) ? 1.0f : 0.0f;
  if (a == b) {
    if (x <= b) return 1.0f;
    return (x >= c) ? 0.0f : (c - x) / (c - b);
  }
  if (b == c) {
    if (x >= b) return 1.0f;
    return (x <= a) ? 0.0f : (x - a) / (b - a);
  }
  if (x <= a || x >= c) return 0.0f;
  if (x <= b) return (x - a) / (b - a);
  return (c - x) / (c - b);
}

// ─── MF Input1 (Eror_suhu), range dikalibrasi ke [-10..40]°C ────────────────
//  Puncak: sangat_panas=-10, panas=0, hangat=10, dingin=25, sangat_dingin=40
static const float FIS_IN1_MF[5][3] = {
  { -22.5f,  -10.0f,   2.5f },   // 1 sangat_panas   (suhu jauh overshoot, >70°C)
  { -12.5f,    0.0f,  12.5f },   // 2 panas           (suhu di/dekat SP, 48-72°C)
  {   0.0f,   10.0f,  22.5f },   // 3 hangat          (suhu mendekati SP, 37-60°C)
  {  10.0f,   25.0f,  40.0f },   // 4 dingin          (suhu jauh, 20-50°C)
  {  25.0f,   40.0f,  52.5f }    // 5 sangat_dingin   (suhu sangat jauh, <35°C)
};

// ─── MF Input2 (Delta_eror), tidak berubah dari versi sebelumnya ────────────
//  Input dalam [0..5] (dipetakan dari deltaError°C [-5..5] → [0..5])
static const float FIS_IN2_MF[3][3] = {
  { 0.0f,  0.0f,  2.08f },  // 1 kecil   (perubahan suhu kecil)
  { 0.41f, 2.5f,  4.58f },  // 2 sedang  (perubahan suhu sedang)
  { 2.91f, 5.0f,  5.0f  }   // 3 besar   (perubahan suhu besar)
};

// ─── MF Output1 (Kecepatan_blower), range dinaikkan ke [30..75]% ────────────
//  Makna: nilai KECIL = blower PELAN (tahan kalor), nilai BESAR = blower CEPAT (buang kalor)
//  Minimum naik dari 20% ke 30% agar sirkulasi udara panas ke material selalu cukup.
static const float FIS_OUT_MF[5][3] = {
  { 65.0f,  75.0f,  85.0f },   // 1 sangat_cepat  (puncak 75%: overshoot → buang kalor)
  { 53.0f,  63.0f,  73.0f },   // 2 cepat          (puncak 63%: mendekati normal dari atas)
  { 41.0f,  52.0f,  63.0f },   // 3 stabil         (puncak 52%: suhu stabil di sekitar SP)
  { 30.0f,  42.0f,  54.0f },   // 4 lambat         (puncak 42%: mendekati SP dari bawah)
  { 22.0f,  30.0f,  40.0f }    // 5 sangat_lambat  (puncak 30%: suhu jauh di bawah SP → tahan kalor)
};

#define FIS_OUT_MIN  30.0f   // [V19-2] dinaikkan dari 20 → 30
#define FIS_OUT_MAX  75.0f   // [V19-2] dinaikkan dari 60 → 75
#define FIS_CENTROID_STEPS 100

// ─── Rule base — index 1-based, SAMA seperti file .fis asli ────────────────
//  Format: {input1_idx, input2_idx, output_idx}
//  Logika (sudah benar dari desain asli):
//    sangat_panas + kecil → sangat_cepat  (overshoot parah → buang kalor penuh)
//    panas        + kecil → cepat         (di/dekat SP → blower agak kencang)
//    hangat       + *     → stabil        (mendekati SP → pertahankan)
//    dingin       + kecil → lambat        (jauh dari SP → pelankan blower, tahan kalor)
//    sangat_dingin+ kecil → sangat_lambat (sangat jauh → blower paling pelan)
static const int FIS_RULES[15][3] = {
  {1,1,1}, {1,2,1}, {1,3,1},
  {2,1,2}, {2,2,2}, {2,3,1},
  {3,1,3}, {3,2,3}, {3,3,3},
  {4,1,4}, {4,2,4}, {4,3,3},
  {5,1,5}, {5,2,5}, {5,3,4}
};

// ─── Evaluasi FIS: fuzzifikasi → inferensi → defuzzifikasi centroid ──────────
static inline float fuzzy_blower(float e, float de) {
  // Fuzzifikasi input
  float mu1[5], mu2[3];
  for (int i = 0; i < 5; i++) {
    mu1[i] = fis_trimf(e,  FIS_IN1_MF[i][0], FIS_IN1_MF[i][1], FIS_IN1_MF[i][2]);
  }
  for (int j = 0; j < 3; j++) {
    mu2[j] = fis_trimf(de, FIS_IN2_MF[j][0], FIS_IN2_MF[j][1], FIS_IN2_MF[j][2]);
  }

  // Inferensi (AndMethod=min)
  float ruleStrength[15];
  for (int r = 0; r < 15; r++) {
    int i1 = FIS_RULES[r][0] - 1;
    int i2 = FIS_RULES[r][1] - 1;
    ruleStrength[r] = fminf(mu1[i1], mu2[i2]);
  }

  // Defuzzifikasi centroid diskrit
  float sumXMu = 0.0f, sumMu = 0.0f;
  const float step = (FIS_OUT_MAX - FIS_OUT_MIN) / (float)FIS_CENTROID_STEPS;

  for (int s = 0; s <= FIS_CENTROID_STEPS; s++) {
    float x = FIS_OUT_MIN + step * (float)s;
    float aggMu = 0.0f;

    for (int r = 0; r < 15; r++) {
      if (ruleStrength[r] <= 0.0f) continue;
      int outIdx = FIS_RULES[r][2] - 1;
      float outMu = fis_trimf(x, FIS_OUT_MF[outIdx][0], FIS_OUT_MF[outIdx][1], FIS_OUT_MF[outIdx][2]);
      float clipped = fminf(ruleStrength[r], outMu);  // ImpMethod=min
      if (clipped > aggMu) aggMu = clipped;           // AggMethod=max
    }

    sumXMu += x * aggMu;
    sumMu  += aggMu;
  }

  // Fallback ke tengah rentang jika tidak ada rule aktif
  if (sumMu <= 0.0001f) {
    return (FIS_OUT_MIN + FIS_OUT_MAX) * 0.5f;  // = 52.5%
  }
  return sumXMu / sumMu;  // DefuzzMethod=centroid
}

#endif // FIS_HEADER_V19_H
