# Rencana Perombakan v2 — Dua Profil (Wafi: Suhu · Fadel: Kecepatan)

> **Status:** ✅ **SEMUA FASE SELESAI & TERVERIFIKASI** (compile OK + mini-test/headless tiap chunk).
> Acuan arsitektur dasar: [ARCHITECTURE.md](ARCHITECTURE.md).
>
> Ringkas yang sudah jadi: (1) params tunable + persist NVS + fix sinkron input web ·
> (2) data logger 5 dtk folder per-profil + tab grafik riwayat (metrik rise/overshoot/osilasi,
> penanda sentuh SP, download CSV/JPG) · (3) modul VFD Modbus RTU (CRC cocok dokumen) + EMA encoder ·
> (4) profil Wafi/Fadel (1 firmware) + PID kecepatan VFD · (5) web 2 tab (Wafi suhu / Fadel kecepatan).

---

## 🎯 Ringkasan tujuan

Menambah **2 profil kontrol** dalam satu firmware, plus perbaikan logger, sinkron web,
kurva kontrol, dan parameter tunable yang persist.

| Profil (tab web) | Mengontrol | Yang DISET & dibuat konstan |
|---|---|---|
| **Wafi** | **Suhu** (fuzzy + FoPID → blower) | sudut gas (servo), kecepatan motor (freq VFD), durasi |
| **Fadel** | **Kecepatan** motor (PID → VFD via Modbus RTU) | sudut gas (servo), blower (%), durasi |

---

## 1. Sinkronisasi input web ↔ ESP32  *(bug)*

**Masalah:** nilai input (mis. sudut servo) di web ke-*overwrite* terus oleh telemetri,
sehingga susah mengetik.

**Solusi:** field input di-*load* **sekali** saat pertama konek (initial snapshot), lalu
**tidak disentuh lagi** kecuali user menekan Set (echo nilai baru). Telemetri tetap update
kartu/grafik, tapi **tidak** field input.

## 2. Data logger + grafik + riwayat

**Perekaman (live):**
- Rekam tiap **5 detik**, mulai saat tombol *Mulai* ditekan; **t₀ = saat itu**.
- Buffer **RAM** untuk grafik live (dihapus saat stop; mulai lagi = dari awal).
- **Durasi habis → berhenti otomatis** (safety supervisor).
- Sekaligus ditulis ke **CSV di SD**, **folder TERPISAH per profil**:
  - Wafi  → `/log/wafi/wafi_NNN.csv`
  - Fadel → `/log/fadel/fadel_NNN.csv`

**Tampilan grafik:**
- Lebar window grafik = **sepanjang durasi** (semua titik muat). Sumbu-x = waktu sejak
  **t₀** (satuan menit:detik, di pojok kiri-bawah).

**Riwayat (card daftar log):**
- Card berisi **daftar** file log (per profil/folder). Klik salah satu → render grafiknya.
- Pada tampilan grafik: tombol **Download CSV** + **Download JPG**
  (JPG via `canvas.toDataURL` di browser — tanpa beban ESP32).

**Metrik otomatis pada grafik** (dihitung client-side dari data):
| Metrik | Definisi usulan |
|---|---|
| Rise time (menit) | t₀ → pertama mencapai SP (dalam pita ±1%) |
| Overshoot % | (puncak − SP) / SP × 100 |
| Osilasi % | ripple puncak-ke-puncak setelah stabil / SP × 100 |

> ❓ Definisi metrik mohon dikonfirmasi (standar skripsi bisa beda, mis. overshoot relatif
> terhadap step `SP−T₀`, rise time 10–90%). → [Q5](#q5)

## 3. Kurva kontrol blower (Wafi) — revisi

Permintaan (relatif terhadap setpoint):

| Kondisi suhu | Blower | Catatan |
|---|---|---|
| Jauh di bawah SP (mis. 27°C) | **30%** | mulai memanaskan lembut |
| Mendekati SP | turun menuju **20%** | "semakin kecil semakin panas" (20 = panas maks) |
| Dalam **±1%** SP | **0%** (mati) | deadband |
| Di atas SP | **60–100%** | mendinginkan |

- ❓ Arah pendinginan: "semakin **mendekati** SP, % makin besar (→100)". Ini terbalik dari
  intuisi (biasanya makin **jauh**/overshoot makin besar). Perlu konfirmasi → [Q1](#q1)
- ❓ Memanaskan makin kuat saat mendekati SP lalu mati mendadak (±1%) berpotensi overshoot/
  osilasi. Dikompensasi karena parameter **tunable**. Konfirmasi bentuk → [Q1](#q1)

## 4. Parameter tunable + persist (NVS/Preferences)

- Semua parameter dapat di-tune dari web, punya **default** di `config.h`, **disimpan ke
  Preferences**, dan **di-load saat boot**.
- Wafi: `Kp Ki Kd λ µ β`, setpoint suhu, sudut gas, durasi, **freq motor (baru)**.
- Fadel: `Kp Ki Kd` (motor), setpoint kecepatan, sudut gas, **blower %**, durasi.

## 5. Tab Fadel — kontrol kecepatan VFD (Modbus RTU)

- PID close-loop: input **encoder RPM** → output **freq VFD** via Modbus RTU (FC06 `0x2001`),
  run/stop via `0x2000`. Panduan: [VFD_Modbus_ESP32_MAX485.md](VFD_Modbus_ESP32_MAX485.md).
- Bisa **Mulai/Berhenti PID**, tune `Kp Ki Kd`, set sudut gas, blower %, durasi, setpoint kecepatan.
- **Encoder RPM tidak stabil → tambah EMA filter** (`rpm_ema = α·rpm + (1-α)·rpm_ema`).

## 6. Rencana pin (⚠️ ada konflik — perlu dikunci)

Dokumen Modbus memakai GPIO25/26/27, tetapi **GPIO25 sudah dipakai Encoder A**, dan
**Serial2 dipakai PZEM**. Usulan penyelesaian:

| Fungsi | Pin lama | **Usulan baru** | Alasan |
|---|---|---|---|
| Encoder A / B | 25 / 33 | **tetap 25 / 33** | hindari pindah sensor utama |
| VFD MAX485 DE/RE | (doc: 25) | **GPIO4** | 25 bentrok encoder |
| VFD MAX485 RX / TX | (doc: 26/27) | **26 / 27** (UART **Serial1**) | bebas |
| PZEM | Serial2 16/17 | **tetap Serial2 16/17** | tak bentrok dgn Serial1 |

> ESP32 punya 3 UART: Serial0 (USB), **Serial1 = VFD**, **Serial2 = PZEM**. → [Q3](#q3)

---

## 🏗️ Arsitektur yang diusulkan

- **Satu firmware**, satu `enum Profile { PROF_WAFI, PROF_FADEL }` di `SystemState`.
- Tab web memilih profil (atau tombol). Profil aktif menentukan loop kontrol mana yang jalan:
  - WAFI: `controlTempCompute()` → blower; VFD di-set freq konstan; servo konstan.
  - FADEL: `controlSpeedCompute()` (PID) → VFD freq; blower konstan; servo konstan.
- Safety supervisor (suhu) **tetap aktif di kedua profil**.
- Modul baru: `vfd.*` (Modbus RTU), `control_speed.*` (PID kecepatan), `datalog.*` (buffer grafik),
  `params.*` (NVS load/save). UI/web dapat 2 tab.

---

## ✅ Keputusan terkunci

- **Q1 Kurva (Wafi):** memanaskan jauh→**30%** · dekat SP→**20%** · ±1% SP→**0%** ·
  di atas SP mendinginkan, **makin jauh makin besar (→100%)** [konvensional].
- **Q2 Arsitektur:** **satu firmware**, switch profil Wafi/Fadel via tab web.
- **Q3 Pin VFD:** DE/RE→**GPIO4**, **Serial1** RX/TX **26/27**; encoder tetap **25/33**;
  PZEM tetap **Serial2 16/17**.
- **Q4 Logger:** buffer **RAM** (grafik) **+** **CSV ke SD** (folder terpisah per profil).

## ❓ Tersisa untuk dikonfirmasi

### Q5 — Definisi metrik grafik
Rise time / overshoot % / osilasi % memakai definisi usulan di [Bagian 2](#2-data-logger--grafik--riwayat),
atau ada definisi spesifik skripsi yang harus dipakai?

---

## 🧭 Fase implementasi (usulan, setelah keputusan dikunci)

1. Params + NVS persist · perbaikan sinkron input web.
2. Kurva kontrol Wafi baru (fuzzy + deadband) — verifikasi via `tools/control_sim.py`.
3. Data logger 5s + tab grafik.
4. Modul `vfd.*` + Modbus + tes loopback/echo.
5. Profil Fadel: PID kecepatan + EMA encoder.
6. UI 2 tab (Wafi/Fadel) + input freq motor di Wafi.
7. Integrasi, uji, update ARCHITECTURE.md & README.
