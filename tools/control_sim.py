#!/usr/bin/env python3
# =============================================================================
#  control_sim.py — Digital twin kontrol blower (cocok dgn firmware).
#
#  Kontroler (FIS + FoPID) di sini PERSIS seperti firmware:
#    ESP32_Firmware/Fis_Header.h  (tabel MF & rule)
#    ESP32_Firmware/control.cpp   (FoPID °C, gabung, clamp [25..85])
#    ESP32_Firmware/config.h      (gain & clamp)
#
#  PLANT di sini hanya MODEL ilustratif yg cocok dgn deskripsi pemilik alat:
#    blower 0-10% mendinginkan · 20-30% memanaskan (puncak ~25%) · 30-85% mendinginkan
#  Pada alat nyata titik-hold (blower saat suhu = SP) ditentukan oleh bukaan gas;
#  suku integral FoPID akan menyesuaikan sendiri. Ubah Tss() utk plant Anda.
#
#  Jalankan:  python3 tools/control_sim.py
# =============================================================================
import math

# ───────── FIS (samakan dgn Fis_Header.h) ─────────
def trimf(x, a, b, c):
    if a == b == c: return 1.0 if x == b else 0.0
    if a == b: return 1.0 if x <= b else (0.0 if x >= c else (c - x) / (c - b))
    if b == c: return 1.0 if x >= b else (0.0 if x <= a else (x - a) / (b - a))
    if x <= a or x >= c: return 0.0
    return (x - a) / (b - a) if x <= b else (c - x) / (c - b)

IN1 = [(-30,-15,-6), (-12,-6,0), (-4,0,5), (3,12,24), (16,28,46)]        # error °C: panas→dingin
IN2 = [(0,0,2.08), (0.41,2.5,4.58), (2.91,5,5)]                          # delta (netral)
OUT = [(66,80,94), (35,50,66), (27,30,34), (25,27,30), (22,25,28)]       # blower %: dingin→panas
OMIN, OMAX = 22.0, 90.0
RULES = [(i, j, i) for i in range(5) for j in range(3)]                  # error-dominant

def fuzzy(e, de):
    mu1 = [trimf(e, *IN1[i]) for i in range(5)]
    mu2 = [trimf(de, *IN2[j]) for j in range(3)]
    rs = [min(mu1[r[0]], mu2[r[1]]) for r in RULES]
    n = 120; num = den = 0.0
    for s in range(n + 1):
        x = OMIN + (OMAX - OMIN) * s / n; agg = 0.0
        for k, r in enumerate(RULES):
            if rs[k] <= 0: continue
            c = min(rs[k], trimf(x, *OUT[r[2]]))
            if c > agg: agg = c
        num += x * agg; den += agg
    return 30.0 if den < 1e-6 else num / den

# ───────── FoPID (samakan dgn config.h / control.cpp) ─────────
KP, KI, KD = 0.45, 0.12, 0.20
LAM, MU = 0.90, 0.92
DT, BETA = 0.5, 0.80
I_CLAMP, D_CLAMP, U_CLAMP = 60.0, 40.0, 14.0
BMIN, BMAX = 25.0, 85.0          # rentang kerja monoton (25=panas maks .. 85=dingin maks)

class Controller:
    def __init__(self): self.integ = 0.0; self.prevE = 0.0
    def reset(self, T, SP): self.integ = 0.0; self.prevE = SP - T
    def step(self, T, SP):
        e = SP - T; de = e - self.prevE; self.prevE = e
        self.integ += (DT ** LAM) * e
        self.integ = max(-I_CLAMP, min(I_CLAMP, self.integ))
        deriv = (DT ** -MU) * de
        deriv = max(-D_CLAMP, min(D_CLAMP, deriv))
        u = KP * e + KI * self.integ + KD * deriv
        u = max(-U_CLAMP, min(U_CLAMP, u))
        fis = fuzzy(max(-20, min(40, e)), max(0, min(5, (de + 5) / 10 * 5)))
        b = fis - u * BETA                       # BLOWER_IS_COOLER: koreksi dikurangi
        return max(BMIN, min(BMAX, b)), fis

# ───────── Plant ilustratif (puncak panas b≈25, Tss(30)=SP nominal) ─────────
TAMB = 28.0
def Tss(b): return TAMB + 47.0 * math.exp(-((b - 25.0) / 8.0) ** 2)
TAU = 45.0

def run(SP, T0, secs, label):
    c = Controller(); c.reset(T0, SP); T = T0; peak = T; b58 = None; rows = []
    for s in range(int(secs / DT)):
        b, fis = c.step(T, SP)
        T += (Tss(b) - T) / TAU * DT
        peak = max(peak, T)
        if b58 is None and abs(T - 58) < 0.5: b58 = b
        rows.append((s * DT, T, b))
    print(f"\n=== {label}  SP={SP}  T0={T0} ===")
    for t, T_, b in rows[::60]:
        print(f"  t={t:4.0f}s  T={T_:6.2f}C  blower={b:5.1f}%  ({'memanaskan' if 20<=b<=30 else 'mendinginkan'})")
    print(f"  -> final T={rows[-1][1]:.2f}C (err {SP-rows[-1][1]:+.2f})  blower={rows[-1][2]:.1f}%  "
          f"overshoot={peak-SP:+.2f}  blower@58C={b58:.1f}%")

if __name__ == "__main__":
    print("Cek statik (error = SP - T, SP=60):")
    for T in [25, 40, 52, 58, 60, 62, 70]:
        b = fuzzy(max(-20, min(40, 60 - T)), 2.5)
        print(f"  T={T:>4}C  fuzzy={b:5.1f}%  -> {'memanaskan(20-30)' if 20<=b<=30 else 'mendinginkan'}")
    run(60, 28, 700, "Pemanasan dari ambient")
    run(60, 70, 420, "Pendinginan dari overshoot")
    run(75, 28, 700, "Setpoint tinggi")
