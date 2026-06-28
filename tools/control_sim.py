#!/usr/bin/env python3
# =============================================================================
#  control_sim.py — Digital twin kontrol blower Wafi (KURVA v2).
#  Mirror dari: Fis_Header.h (MF) + control.cpp (deadband + FoPID trim) + config.h.
#  PLANT = model ilustratif (puncak panas blower ~20%); ganti Tss() utk plant Anda.
#  Jalankan:  python3 tools/control_sim.py   (butuh matplotlib utk PNG, opsional)
# =============================================================================
import math
try:
    import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt; HAVE_PLT = True
except Exception:
    HAVE_PLT = False

# ── FIS (samakan Fis_Header.h) ──
def trimf(x, a, b, c):
    if a == b == c: return 1.0 if x == b else 0.0
    if a == b: return 1.0 if x <= b else (0.0 if x >= c else (c - x) / (c - b))
    if b == c: return 1.0 if x >= b else (0.0 if x <= a else (x - a) / (b - a))
    if x <= a or x >= c: return 0.0
    return (x - a) / (b - a) if x <= b else (c - x) / (c - b)

IN1 = [(-45,-30,-8), (-18,-6,0), (0,6,18), (8,30,55)]      # error °C, panas→dingin
OUT = [(90,100,108), (48,60,72), (14,20,26), (24,30,40)]   # blower %
IN2 = [(0,0,2.08), (0.41,2.5,4.58), (2.91,5,5)]
OMIN, OMAX = 10.0, 108.0
RULES = [(i, j, i) for i in range(4) for j in range(3)]

def fuzzy(e, de):
    mu1 = [trimf(e, *IN1[i]) for i in range(4)]; mu2 = [trimf(de, *IN2[j]) for j in range(3)]
    rs = [min(mu1[r[0]], mu2[r[1]]) for r in RULES]; n = 120; num = den = 0.0
    for s in range(n + 1):
        x = OMIN + (OMAX - OMIN) * s / n; agg = 0.0
        for k, r in enumerate(RULES):
            if rs[k] <= 0: continue
            c = min(rs[k], trimf(x, *OUT[r[2]]))
            if c > agg: agg = c
        num += x * agg; den += agg
    return 20.0 if den < 1e-6 else num / den

# ── FoPID + deadband (samakan control.cpp / config.h) ──
SP = 60.0
KP, KI, KD = 0.20, 0.03, 0.05; LAM, MU = 0.90, 0.92; BETA = 0.50; DT = 0.5
I_CLAMP, D_CLAMP, U_CLAMP = 60.0, 40.0, 14.0
DEADBAND_PCT, DEADBAND_BLOWER, DIMMER_MAX = 1.0, 0, 100

class Ctrl:
    def __init__(self): self.integ = 0.0; self.pe = SP
    def step(self, T):
        e = SP - T; de = e - self.pe; self.pe = e
        band = DEADBAND_PCT / 100.0 * SP
        if abs(e) <= band:
            self.integ = 0.0; return float(DEADBAND_BLOWER)
        self.integ = max(-I_CLAMP, min(I_CLAMP, self.integ + (DT ** LAM) * e))
        deriv = max(-D_CLAMP, min(D_CLAMP, (DT ** -MU) * de))
        u = max(-U_CLAMP, min(U_CLAMP, KP * e + KI * self.integ + KD * deriv))
        fis = fuzzy(max(-30, min(40, e)), max(0, min(5, (de + 5) / 10 * 5)))
        return max(0.0, min(float(DIMMER_MAX), fis - u * BETA))

# ── Plant ilustratif: puncak panas blower ~20% ──
TAMB = 27.0
def Tss(b): return TAMB + 42.0 * math.exp(-((b - 20.0) / 19.2) ** 2)
TAU = 45.0

def run(secs=1200):
    c = Ctrl(); T = TAMB; ts = []; Ts = []; Bs = []
    for s in range(int(secs / DT)):
        b = c.step(T); T += (Tss(b) - T) / TAU * DT
        ts.append(s * DT); Ts.append(T); Bs.append(b)
    return ts, Ts, Bs

def metrics(ts, Ts):
    band = DEADBAND_PCT / 100.0 * SP
    rise = next((t for t, x in zip(ts, Ts) if abs(x - SP) <= band), None)
    peak = max(Ts); over = (peak - SP) / SP * 100
    seg = [x for t, x in zip(ts, Ts) if rise is not None and t >= rise]
    osil = (max(seg) - min(seg)) / SP * 100 if seg else float('nan')
    return rise, over, osil

if __name__ == "__main__":
    print("Statik fuzzy+deadband (T -> blower):")
    cc = Ctrl()
    for T in [27, 40, 55, 58, 59.5, 60, 62, 70, 80]:
        cc.integ = 0; cc.pe = SP - T
        print(f"  T={T:>5}  blower={cc.step(T):5.1f}%")
    ts, Ts, Bs = run(1200)
    rise, over, osil = metrics(ts, Ts)
    print(f"\nRise time : {rise/60:.2f} menit" if rise else "Rise: tak tercapai")
    print(f"Overshoot : {over:.2f} %")
    print(f"Osilasi   : {osil:.2f} %")
    print(f"Final T   : {Ts[-1]:.2f} C")
    if HAVE_PLT:
        tm = [t / 60 for t in ts]
        fig, ax = plt.subplots(2, 1, figsize=(9, 6), sharex=True)
        ax[0].plot(tm, Ts, '#ff7a2f', lw=2, label='Suhu'); ax[0].axhline(SP, ls='--', color='#ef4444', label='SP')
        if rise: ax[0].plot(rise/60, SP, 'o', color='#22d3ee', ms=9, label=f'sentuh SP {rise/60:.1f}m')
        ax[0].set_ylabel('Suhu (C)'); ax[0].grid(alpha=.2); ax[0].legend(fontsize=8)
        ax[0].set_title(f'Wafi v2 | rise {rise/60:.1f}m  overshoot {over:.1f}%  osilasi {osil:.1f}%')
        ax[1].plot(tm, Bs, '#3ec6e0', lw=1.3); ax[1].set_ylabel('Blower (%)'); ax[1].set_xlabel('menit'); ax[1].grid(alpha=.2)
        plt.tight_layout(); plt.savefig('/tmp/control_sim.png', dpi=90); print("PNG -> /tmp/control_sim.png")
