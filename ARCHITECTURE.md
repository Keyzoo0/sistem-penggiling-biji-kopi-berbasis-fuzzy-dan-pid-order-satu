# Blueprint Arsitektur & Flow — Sistem Kontrol Suhu Biji Kopi

> **Status:** ✅ Diimplementasikan — modular + **dual-core FreeRTOS** · compile OK (arduino-cli, esp32 core 3.3.10, 94% flash).
> **Konteks:** Alat **produksi nyata** · keselamatan nomor satu · **full-modular**.
> **Tujuan dokumen:** Menjadi acuan tunggal yang disepakati sebelum kode dirombak.
> Setiap perubahan besar pada flow/arsitektur mengacu & memperbarui dokumen ini.

---

## Daftar Isi

1. [Tujuan & Prinsip Desain](#1-tujuan--prinsip-desain)
2. [Keputusan Terkunci (Design Contract)](#2-keputusan-terkunci-design-contract)
3. [Model Plant — WAJIB dibaca](#3-model-plant--wajib-dibaca)
4. [Arsitektur Modul](#4-arsitektur-modul)
5. [Model Konkurensi (Command Queue)](#5-model-konkurensi-command-queue)
6. [Operating State Machine](#6-operating-state-machine)
7. [UI Navigation State (terpisah)](#7-ui-navigation-state-terpisah)
8. [Safety Supervisor](#8-safety-supervisor)
9. [Flow Kontrol (FIS + FoPID)](#9-flow-kontrol-fis--fopid)
10. [Siklus Logging](#10-siklus-logging)
11. [Urutan Eksekusi loop()](#11-urutan-eksekusi-loop)
12. [Struktur Data](#12-struktur-data)
13. [Kontrak Antarmuka Web](#13-kontrak-antarmuka-web)
14. [Konfigurasi (config.h)](#14-konfigurasi-configh)
15. [Rencana Migrasi Bertahap](#15-rencana-migrasi-bertahap)
16. [Item Terbuka & Tindak Lanjut](#16-item-terbuka--tindak-lanjut)
17. [Lampiran: Masalah Lama → Solusi](#17-lampiran-masalah-lama--solusi)

---

## 1. Tujuan & Prinsip Desain

Sistem mengontrol **suhu** biji kopi pada proses pengeringan/roasting dengan
aktuator **blower (dimmer AC)** dan sumber panas **gas (servo katup)**.

Prinsip yang memandu seluruh rancangan:

| # | Prinsip | Konsekuensi |
|---|---------|-------------|
| P1 | **Safety-first** | Guard suhu dievaluasi tiap loop, prioritas di atas segalanya. |
| P2 | **Satu sumber kebenaran** | Semua state ada di `SystemState`; hanya `loop()` yang menulis. |
| P3 | **Layar ≠ kondisi mesin** | UI nav state terpisah total dari operating state. |
| P4 | **Input = perintah, bukan tulisan langsung** | Keypad & web menitip *command* ke antrian. |
| P5 | **Non-blocking** | Tidak ada `delay()` di alur kerja; semua via transisi state. |
| P6 | **Eksplisit & terkonfigurasi** | Ambang, arah kontrol, pin → konstanta bernama di `config.h`. |
| P7 | **Modular** | Tiap subsistem 1 unit kompilasi; dependensi satu arah. |

---

## 2. Keputusan Terkunci (Design Contract)

Hasil diskusi dengan pemilik alat — dijadikan kontrak desain:

| Aspek | Keputusan |
|---|---|
| Konteks proyek | Alat **produksi**; algoritma dipertahankan **kecuali jelas salah**. |
| Struktur | **Full-modular** (banyak file `.h/.cpp`). |
| Safe state (darurat) | **Gas tutup (servo 0°) + blower mati (0%)** + alarm. |
| Otoritas web | **Kontrol penuh** (start/set/stop), **tanpa konflik** dengan keypad/LCD. |
| Mode manual | **Dipertahankan & dirapikan** (dilepas dari setpoint suhu). |
| Variabel terkontrol | **Suhu** (bukan kecepatan). |
| RPM drum | **Monitoring + warning** saja → **tidak** memicu FAULT. |
| Pin 12 (LED) | **Indikator status** (bukan pemanas, bukan safety). |
| Pemicu FAULT | Guard **suhu**: over-temp, sensor gagal, E-STOP. |
| Arah kontrol blower | Lihat [Bagian 3](#3-model-plant--wajib-dibaca) — *pivot 30%*. |
| Tuning kontrol | Dipercayakan ke rancangan ini; final divalidasi via **step test**. |

---

## 3. Model Plant — WAJIB dibaca

**Temuan kunci dari pemilik alat:** blower bersifat **dua arah** dengan titik
netral (*pivot*) di sekitar **30%**.

```
   0% ───────────────── 30% ───────────────── 100%
   │   regime PEMANAS     │     regime PENDINGIN   │
   │   (kalor tertahan,   ▲     (kalor terbuang,   │
   │    suhu cenderung↑)  │      suhu cenderung↓)  │
                       pivot ≈ 30% (netral)
```

- **Sumber panas = gas burner** (servo katup, dioperasikan **manual/fixed**).
- **0–10%** → mendinginkan (aliran terlalu kecil, kalor burner tak terbawa).
- **20–30%** → **memanaskan** (aliran optimal; **puncak** transfer panas ~25%).
- **30–85%** → mendinginkan (aliran berlebih → kalor terbuang).

> **REVISI — model NON-MONOTON (bukit, puncak panas ~25%).** Hubungan blower→suhu
> bukan tangga sederhana. Agar kontrol monoton, kontroler beroperasi di sisi kanan
> puncak **[25..85]%**: 25=panas maks · 30=hold tepat di SP · 85=dingin maks.
> Metode + parameter **diverifikasi via `tools/control_sim.py`** (28→60°C settle 60.0
> blower 30; di 58°C blower 25 = memanaskan). Bug lama: FIS [30..75]% (semua zona
> dingin) → 58°C mendinginkan. Diperbaiki di `Fis_Header.h` (output [22..90]%).

### Implikasi terhadap kode lama (akar masalah "suhu tak pernah 60°C")

1. `FIS_OUT_MIN = 30` mengurung output FIS di **[30..75]%** → FIS **terkunci di
   regime pendingin**; secara fisik tak pernah bisa membantu memanaskan.
2. Revisi v19 menaikkan minimum **20%→30%** → justru **menambah pendinginan** →
   makin sulit mencapai SP. (v18 dengan min 20% sebenarnya lebih dekat benar.)
3. **Bentuk FIS sudah benar** (dingin→blower kecil, panas→blower besar); yang
   salah hanya **rentang outputnya tergeser ke atas** sehingga kehilangan akses
   ke regime pemanas.
4. **FoPID terbalik tanda** terhadap plant ini (lihat [Bagian 9](#9-flow-kontrol-fis--fopid)).

> ⚠️ Karakterisasi pasti (nilai pivot eksak, otoritas pemanasan di bawah pivot)
> sebaiknya diperoleh lewat **step test open-loop** — lihat [Bagian 16](#16-item-terbuka--tindak-lanjut).

---

## 4. Arsitektur Modul

```
ESP32_Firmware/
├── ESP32_Firmware.ino   # setup() init+self-test · loop() scheduler
├── config.h             # pin · ambang · arah kontrol · kredensial · default
├── types.h              # enum & struct (SystemState, Command, FaultCode, ...)
├── state.h / .cpp       # SystemState (sumber kebenaran) + command queue
├── sensors.h / .cpp     # MLX90614 · PZEM · encoder→RPM · DS3231
├── actuators.h / .cpp   # dimmer(blower) · servo(gas) · LED · applySafeState()
├── control.h / .cpp     # FoPID + Fuzzy (pakai Fis_Header.h)
├── safety.h / .cpp      # supervisor: guard suhu + countdown → FAULT
├── ui.h / .cpp          # render LCD (dari state) + keypad → command
├── webserver.h / .cpp   # http + ws: baca snapshot, tulis via command
├── logging.h / .cpp     # SD CSV; buka/tutup terikat transisi RUNNING
├── Fis_Header.h         # (dipertahankan) fuzzy inference
└── data/                # index.html · app.js · style.css
```

### Aturan dependensi (satu arah, tanpa siklus)

```
            ┌────────────┐
            │  config.h  │  ◄── di-include semua
            │  types.h   │
            └────────────┘
                  ▲
            ┌────────────┐
            │  state.*   │  ◄── SystemState + queue (dipakai semua modul)
            └────────────┘
       ┌────────┬─────────┬─────────┬─────────┐
   sensors   actuators  control   logging   safety
       └────────┴────┬────┴─────────┴─────────┘
                ui.*   webserver.*        (lapisan input/output)
                     ▲
              ESP32_Firmware.ino (orkestrasi)
```

- Modul **tidak** saling memanggil sembarangan; mereka berkomunikasi lewat
  `SystemState` + command queue.
- `ui.*` & `webserver.*` **hanya membaca** state untuk tampilan dan **menulis
  lewat command**, tak pernah mengubah state langsung.

---

## 5. Model Konkurensi (Command Queue)

Masalah lama: handler WebSocket/REST jalan di task **AsyncTCP (core berbeda)**
tetapi menulis variabel global & mengakses SD/servo bersamaan dengan `loop()` →
*race condition*.

Solusi: **semua input menitip command**, `loop()` satu-satunya penulis.

```
Keypad/LCD ─┐
            ├─ enqueue(Command) ─► [ RING BUFFER thread-safe ] ─┐
Web / WS   ─┘                                                   │
                                                                ▼
                              loop():  drainCommands() → terapkan ke SystemState
                                                                │
                              LCD & Web  ◄── render dari snapshot SystemState
```

- **Enqueue** dari sumber mana pun (aman, hanya menambah ke antrian).
- **Drain** sekali per loop di titik yang ditentukan (lihat [Bagian 11](#11-urutan-eksekusi-loop)).
- **Perintah terakhir menang**; **E-STOP** diberi prioritas (diproses lebih dulu).
- Inilah yang membuat "web kontrol penuh" **tidak konflik** dengan keypad: kedua
  sumber menulis lewat jalur yang sama, dan kedua tampilan mencerminkan state
  yang sama.

**Implementasi nyata (FreeRTOS):** antrian = `xQueueCreate(16, sizeof(Command))`.
State dijaga 3 mutex: `g_stateMux` (SystemState), `g_i2cMux` (bus I2C — wajib karena
MLX dibaca di core 1 sedangkan LCD/keypad/RTC di core 0), `g_sdMux` (kartu SD).

### Task & core

| Task | Core | Prio | Periode | Tugas |
|---|---|---|---|---|
| `realtimeTask` | 1 | 3 | 100 ms | suhu → drain command → safety → kontrol (500ms) → aktuator → publish |
| `pzemTask` | 0 | 1 | 1000 ms | baca PZEM → tulis field daya |
| `uiTask` | 0 | 2 | 80 ms | render LCD + scan keypad → command |
| `logTask` | 0 | 1 | 500 ms | append CSV (SD) |
| `wsTask` | 0 | 1 | 500 ms | broadcast WebSocket |
| AsyncTCP | 0 | — | event | handler HTTP/WS → command |

**Penulis state proses hanya `realtimeTask`** (lewat working-copy lokal lalu
`publishState`); `pzemTask` hanya menulis field daya. Urutan lock konsisten
**SD→I2C** (tidak pernah I2C→SD) sehingga bebas deadlock; akses I2C dari core 1
pakai `I2C_TRYLOCK` agar kontrol tak pernah ter-blok permanen.

---

## 6. Operating State Machine

State **mesin** (bukan layar). Hanya `loop()`/safety yang mengubahnya.

```
   ┌──────┐ self-test OK      ┌────────┐
   │ BOOT │──────────────────►│  IDLE  │◄──────────────────┐
   └──────┘                   └────────┘                    │ CMD_RESET
       │ sensor kritis hilang      │ CMD_START(mode)         │ (ack operator)
       ▼                           ▼                         │
   ┌────────┐                ┌──────────────┐                │
   │ FAULT  │                │   RUNNING     │               │
   │ (aman) │◄───────────────│  sub: FUZZY   │               │
   └────────┘  guard suhu    │     | MANUAL  │               │
       ▲       / E-STOP       └──────────────┘               │
       │                           │ countdown habis          │
       │ guard dari state apa pun  ▼                          │
       │                     ┌──────────┐                     │
       └─────────────────────│ FINISHED │─────────────────────┘
                             │  (aman)  │
                             └──────────┘
```

### Tabel transisi

| Dari | Event | Ke | Aksi saat transisi |
|---|---|---|---|
| BOOT | self-test OK | IDLE | inisialisasi tampilan; aktuator posisi aman |
| BOOT | sensor kritis gagal | FAULT | safe state; tampil error |
| IDLE | `CMD_START(FUZZY/MANUAL)` | RUNNING | reset FoPID; `startLogging()`; set `motorStartTime`; mulai countdown |
| RUNNING | `CMD_STOP` / `CMD_ESTOP` | FAULT* | safe state; `stopLogging()` |
| RUNNING | guard suhu trip | FAULT | safe state; catat `FaultCode`; `stopLogging()` |
| RUNNING | countdown habis | FINISHED | safe state; `stopLogging()`; tampil "ambil SD" |
| FINISHED | `CMD_RESET` (ack) | IDLE | bersihkan tampilan |
| FAULT | `CMD_RESET` (ack) **dan** kondisi sudah aman | IDLE | clear fault |

> \* `CMD_STOP` manual oleh operator boleh menuju IDLE langsung (bukan FAULT) —
> lihat catatan implementasi; `CMD_ESTOP`/guard selalu → FAULT (butuh ack).

**Sub-mode RUNNING:**
- `FUZZY` — closed-loop FIS+FoPID pada suhu.
- `MANUAL` — operator mengatur **% blower** langsung (di-*decouple* dari setpoint
  suhu; menghentikan hack lama `dimmer = setpoint`). Safety & logging tetap aktif.

---

## 7. UI Navigation State (terpisah)

Murni soal **layar mana yang tampil** — **tidak pernah** memengaruhi
kontrol/safety/logging.

```
NAV_HOME ─► NAV_RUNMODE ─► (kirim CMD_START) 
   │
   ├─► NAV_PARAMS ─► NAV_SET_FUZZY / NAV_SET_MANUAL / NAV_SET_DURATION / NAV_SET_SERVO ─► NAV_INPUT
   │
   └─► NAV_MONITOR (page 0..2)   ← bebas dibuka kapan saja, termasuk saat RUNNING
```

- Menavigasi menu **tidak** menghentikan proses; countdown & safety tetap jalan
  (mengatasi bug lama di mana auto-stop hanya aktif di halaman Monitor).
- Render LCD pakai deteksi-perubahan (anti-flicker) seperti sekarang, tapi
  membaca dari `SystemState`.

---

## 8. Safety Supervisor

Dijalankan **setiap iterasi loop**, tak peduli operating/nav state. Begitu satu
guard trip → set `SystemState.opState = FAULT` + `faultCode`.

### Guard (urutan prioritas)

| # | Guard | Kondisi | Aksi |
|---|-------|---------|------|
| 1 | E-STOP | command e-stop dari web/keypad | → FAULT (`FAULT_ESTOP`) |
| 2 | Sensor gagal | suhu `NaN`, atau di luar `[0..150]°C`, atau **beku** (tak berubah) > `SENSOR_TIMEOUT_S` | → FAULT (`FAULT_SENSOR`) |
| 3 | Over-temp | `suhu > SAFE_MAX_TEMP` (mis. 120°C) | → FAULT (`FAULT_OVERTEMP`) |
| 4 | Countdown habis | `RUNNING` & elapsed ≥ durasi | → FINISHED |
| — | RPM | di luar batas (lihat config) | **warning saja** (LCD/web/log), **tidak** FAULT |

### Safe state (saat FAULT & FINISHED)

```c
applySafeState():
    servo → 0°            // gas tertutup
    blower (dimmer) → 0%  // mati
    LED → pola alarm (FAULT) / mati (FINISHED)
    logging → flush + close
```

### Kebijakan reset

- FAULT **tidak auto-recover**. Operator harus menekan reset (keypad/web) **dan**
  kondisi harus sudah aman (mis. suhu sudah < ambang) sebelum kembali ke IDLE.
  Disengaja: paksa operator memeriksa penyebab dulu.

---

## 9. Flow Kontrol (FIS + FoPID)

Hanya dijalankan saat `opState == RUNNING && subMode == FUZZY`, tiap `DT_FIXED`
(500 ms). Di titik ini safety **sudah** dipastikan aman.

> **STATUS IMPLEMENTASI:** metode final mengikuti model non-monoton (§3). FoPID
> dihitung dalam **°C** (bukan %), keluaran kontrol di-clamp ke **[25..85]%**
> (`CTRL_BLOWER_MIN..DIMMER_MAX`), FIS output di-MF-kan ulang ke [22..90]%
> (`Fis_Header.h`). Gain tervalidasi: Kp=0.45 Ki=0.12 Kd=0.20 β=0.80. Lihat
> `tools/control_sim.py` untuk simulasi & penalaan. Bagian rancangan di bawah
> (FIS_OUT_MIN 15-18 dsb.) adalah pemikiran awal — **acuan final = kode + sim**.

```
error   = SP − T                 (+ = terlalu dingin → butuh PANAS)
dError  = error − prevError

blowerBase = FIS(error°C, dError)         // suku fuzzy
koreksi    = FoPID(error)                 // suku fractional-PID
blower%    = clamp( blowerBase  ±  koreksi·beta , DIMMER_MIN , DIMMER_MAX )
             └── tanda mengikuti arah plant (lihat di bawah) ──┘
terapkan blower% ke dimmer
```

### Koreksi yang disepakati (rancangan terbaik)

Mengingat **pivot 30%** dan variabel terkontrol = suhu:

1. **Perbaikan WAJIB — tanda FoPID (jelas salah).**
   Lama: `dimmerRaw = fis + u·beta`. Saat dingin (`error>0`), `u>0` → blower
   **naik** → **mendinginkan** → melawan FIS.
   **Benar:** `dimmerRaw = fis − u·beta`. Saat dingin, FoPID menarik blower
   **turun** (menuju/melewati pivot, ke regime pemanas) → membantu naik ke SP.
   Saat overshoot (`error<0`), blower naik → mendinginkan. ✅ searah FIS.

2. **Buka kembali regime pemanas pada FIS.**
   `FIS_OUT_MIN` diturunkan dari 30 → **±15–18%** (default konservatif) supaya
   saat **sangat dingin**, FIS sendiri sudah memerintahkan blower di **bawah
   pivot** (aktif memanaskan). `FIS_OUT_MAX` tetap 75%. Pivot 30% jadi titik
   netral di sekitar SP. (Nilai eksak difinalkan via step test.)

3. **Arah kontrol via konstanta.** `BLOWER_PIVOT_PCT` & `BLOWER_IS_COOLER`
   (default `true` untuk regime kerja) di `config.h`, agar mudah dibalik bila
   karakterisasi alat berubah — tanpa mengutak-atik logika.

4. **Pertahankan** struktur FIS (5 MF, 15 rule, centroid), gain Kp/Ki/Kd,
   `lambda`, `mu`. Hanya arah & rentang output yang dirapikan agar **koheren
   secara fisik**.

> Catatan metodologis (untuk dokumentasi): aproksimasi fractional saat ini
> (`integral += dt^λ·e`, `derivative = dt^(−µ)·Δe`) adalah penyederhanaan, dan
> derivative dihitung dari *error* (berpotensi *derivative kick* saat SP
> berubah). Dipertahankan demi konsistensi dengan desain awal; dicatat sebagai
> kandidat penyempurnaan bila diperlukan.

### Reset antar sesi

Saat masuk RUNNING: `integral = 0`, `prevError = error sekarang`, `derivative = 0`
→ mencegah windup carry-over dari sesi sebelumnya.

---

## 10. Siklus Logging

Logging terikat **eksklusif** ke transisi state (bukan tersebar di 3 tempat):

| Transisi | Aksi logging |
|---|---|
| → RUNNING | `startLogging()` (buat file unik, tulis header) |
| RUNNING → FINISHED/FAULT/IDLE | `stopLogging()` (flush + close) |
| tiap `intervalSD` saat RUNNING | `logCurrentData()` (append baris) |

- Hanya **satu** pemilik handle file (`loop()`), sehingga `/api/logs` &
  `/api/download` aman (tidak `SD.begin()` ulang saat file terbuka).
- Format CSV & header metadata dipertahankan (sudah baik).

---

## 11. Urutan Eksekusi realtimeTask (core 1)

Loop kontrol nyata ada di `realtimeTask` (bukan `loop()` Arduino, yang dibiarkan
idle). Urutan ini **disengaja** demi keselamatan & konsistensi:

```c
realtimeTask (core 1, tiap 100 ms via vTaskDelayUntil):
  1. baca suhu (MLX, I2C-lock)            5. terapkan aktuator (atau safe state)
  2. baca RPM (tiap 300 ms)               6. LED indikator per operating state
  3. drain command queue (E-STOP dulu)    7. publishState() → g_state
  4. safety supervisor → FAULT/FINISHED      (logging, UI, web = task lain di core 0)
     + kontrol FUZZY (tiap 500 ms)
```

Tidak ada `delay()` di alur kerja; semua task pakai `vTaskDelayUntil` sehingga
task IDLE tetap jalan dan task-watchdog tak pernah trip.

---

## 12. Struktur Data

Sketsa rancangan (final saat implementasi):

```c
// types.h
enum OpState   { ST_BOOT, ST_IDLE, ST_RUNNING, ST_FINISHED, ST_FAULT };
enum RunSubMode{ SUB_NONE, SUB_FUZZY, SUB_MANUAL };
enum FaultCode { FLT_NONE, FLT_ESTOP, FLT_SENSOR, FLT_OVERTEMP };
enum CmdType   { CMD_START, CMD_STOP, CMD_ESTOP, CMD_RESET,
                 CMD_SET_SETPOINT, CMD_SET_SERVO, CMD_SET_BLOWER,
                 CMD_SET_PARAM, CMD_SET_DURATION };

struct Command {
  CmdType type;
  float   fval;     // nilai (setpoint, param, ...)
  int     ival;     // nilai int (servo, blower%, sub-mode, param id)
};

struct SystemState {
  // operasi
  OpState     opState;
  RunSubMode  subMode;
  FaultCode   fault;
  // proses
  float suhu, setPoint, error, dError;
  float uFopid, integral, derivative, fisOut;
  int   blowerPct;        // dimmer aktual
  int   servoDeg;         // sudut gas
  // sensor lain
  float rpmDryer, voltage, current, power, pf;
  int   rpmStatus;        // info/warning saja
  // sesi
  bool          logging;
  uint32_t      runStartMs;
  uint32_t      durationMin;
  char          logFile[32];
  // tunable (mirror dari config, bisa diubah operator)
  float Kp, Ki, Kd, lambda, mu, beta;
};
```

- `SystemState` = **satu instance global**, ditulis hanya oleh `loop()`.
- UI & web membaca; menulis hanya via `enqueueCommand()`.

---

## 13. Kontrak Antarmuka Web

### WebSocket `/ws` — broadcast snapshot (tiap 500 ms, server→klien)

```json
{
  "opState": "RUNNING", "subMode": "FUZZY", "fault": "NONE",
  "temp": 58.5, "setpoint": 60.0, "error": 1.5,
  "blower": 28, "servo": 90, "rpm": 32.5,
  "power": 1500, "voltage": 220.1, "current": 6.8, "pf": 0.98,
  "fisOut": 26.0, "u_fopid": 0.45, "integral": 1.2, "derivative": 0.0,
  "rpmStatus": 1, "logging": true, "remaining": 4200
}
```

### Perintah (klien→server) — diterjemahkan ke `Command`, **masuk antrian**

| Pesan WS | Command |
|---|---|
| `{"start":"FUZZY"}` / `{"start":"MANUAL"}` | `CMD_START` |
| `{"stop":true}` | `CMD_STOP` |
| `{"estop":true}` | `CMD_ESTOP` (prioritas) |
| `{"reset":true}` | `CMD_RESET` |
| `{"setpoint":60.0}` | `CMD_SET_SETPOINT` |
| `{"servo":90}` | `CMD_SET_SERVO` |
| `{"blower":40}` | `CMD_SET_BLOWER` (mode manual) |

### REST (untuk berkas, bukan kontrol real-time)

| Endpoint | Method | Fungsi |
|---|---|---|
| `/` | GET | dashboard (LittleFS) |
| `/api/status` | GET | snapshot JSON (sama dgn WS) |
| `/api/logs` | GET | daftar CSV di SD |
| `/api/download?file=` | GET | unduh CSV |

> Endpoint kontrol REST lama (`/api/setpoint`, `/api/servo`, `/api/stop`) →
> **disatukan ke jalur command** (boleh tetap ada sebagai pembungkus tipis yang
> meng-enqueue, agar tidak ada dua implementasi paralel).

---

## 14. Konfigurasi (config.h)

Semua "angka ajaib" dipindah ke sini sebagai konstanta bernama:

```c
// ── Pin ───────────────────────────────────────────────
#define PIN_SDA 21      #define PIN_SCL 22
#define PIN_DIMMER 32   #define PIN_ZEROCROSS 35
#define PIN_SERVO 13    #define PIN_LED 12
#define PIN_ENC_A 25    #define PIN_ENC_B 33
#define PIN_PZEM_RX 16  #define PIN_PZEM_TX 17
#define PIN_SD_CS 5     // SPI: SCK18 MISO19 MOSI23

// ── Aktuator ──────────────────────────────────────────
#define DIMMER_MIN 0    #define DIMMER_MAX 85
#define SERVO_MAX 180

// ── Model plant / arah kontrol ───────────────────────
#define BLOWER_PIVOT_PCT 30      // titik netral pemanas/pendingin
#define BLOWER_IS_COOLER true    // true: >pivot = mendinginkan (regime kerja)

// ── Safety (suhu) ────────────────────────────────────
#define SAFE_MAX_TEMP   120.0f   // over-temp → FAULT
#define SENSOR_MIN_TEMP   0.0f
#define SENSOR_MAX_TEMP 150.0f
#define SENSOR_TIMEOUT_S   5      // suhu beku N detik → FAULT

// ── RPM (warning saja) ───────────────────────────────
#define RPM_WARN_LOW 23.0f   #define RPM_WARN_HIGH 39.0f
#define RPM_GEAR_RATIO 8.0f      // rpmDryer = rpmEncoder / ratio
#define RPM_STARTUP_SEC 30

// ── Kontrol (default, dapat diubah operator) ─────────
#define DEF_SETPOINT 60.0f
#define DEF_KP 0.60f  #define DEF_KI 0.08f  #define DEF_KD 0.50f
#define DEF_LAMBDA 0.90f  #define DEF_MU 0.92f  #define DEF_BETA 0.40f
#define DT_FIXED 0.5f
#define DEF_DURATION_MIN 75

// ── FIS output (lihat Bagian 9) ──────────────────────
#define FIS_OUT_MIN 18.0f        // diturunkan dari 30 → buka regime pemanas
#define FIS_OUT_MAX 75.0f

// ── Jaringan (pindahkan ke sini / secrets) ───────────
#define WIFI_SSID "..."  #define WIFI_PASS "..."
#define AP_SSID "Kopi-Control"  #define AP_PASS "12345678"
#define HOSTNAME "kopi"
```

---

## 15. Rencana Migrasi Bertahap

Setiap fase **harus tetap kompilasi & jalan**. Tidak "big bang".

| Fase | Isi | Menutup |
|---|---|---|
| **0** | Blueprint ini | — |
| **1** | Ekstrak `config.h` + `types.h` + `state.h` (tanpa ubah perilaku) | S1 (awal) |
| **2** | Command queue + single-writer; keypad & web lewat antrian | **C2**, S4 |
| **3** | Modul `safety.*` + supervisor: pindahkan countdown keluar dari `monitorView`, tambah guard suhu, buang `delay()` | **C1, C3, C4** |
| **4** | Pecah `sensors / actuators / control / logging / ui / webserver` | S1, S3 |
| **5** | Koreksi kontrol: tanda FoPID + `FIS_OUT_MIN` + arah via config | **A1**, A3 (sebagian) |
| **6** | Bersih-bersih: dead code, penamaan, LED jadi indikator status, perbarui README | Q1–Q5 |

Urutan ini memprioritaskan **safety & race** (fase 2–3) sesuai sifat alat produksi,
baru modularisasi penuh & tuning.

---

## 16. Item Terbuka & Tindak Lanjut

1. **Step test open-loop (sangat disarankan).** Kunci gas di beberapa sudut
   (mis. 60/90/120°), kunci blower di 18/30/50/75%, catat suhu steady-state →
   peta plant nyata: konfirmasi pivot eksak & otoritas pemanasan. Hasilnya
   memfinalkan `FIS_OUT_MIN`, peak MF, dan (bila perlu) rule base.
2. **Validasi tanda FoPID** pada alat setelah fase 5 (uji dingin→SP).
3. **Penyempurnaan metodologi FoPID** (Grünwald-Letnikov / derivative-on-measurement)
   — opsional, hanya jika dibutuhkan untuk laporan/akurasi.
4. **Kredensial WiFi** → pertimbangkan file `secrets.h` (git-ignored) atau
   WiFiManager.

---

## 17. Lampiran: Masalah Lama → Solusi

| Kode | Masalah | Solusi di blueprint |
|---|---|---|
| **C1** | Auto-stop countdown hanya jalan di halaman Monitor | Countdown milik **safety supervisor** (Bagian 8), lepas dari UI |
| **C2** | Race web ↔ loop (SD/servo/state) | **Command queue + single-writer** (Bagian 5) |
| **C3** | Safety (over-temp/sensor) diklaim README tapi tak ada | Guard suhu di **supervisor** (Bagian 8) |
| **C4** | `delay()` blocking di alur | Semua via **transisi state non-blocking** (Bagian 11) |
| **S1** | Monolitik 1569 baris, global | **Full-modular** (Bagian 4) |
| **S2** | Model mode kacau; `dimmer=setpoint` | **Operating SM** + sub-mode manual decoupled (Bagian 6) |
| **S3** | Logging tersebar 3 tempat | Terikat **transisi RUNNING** (Bagian 10) |
| **S4** | REST & WS duplikat kontrol | Disatukan ke **command queue** (Bagian 13) |
| **A1** | Tanda FoPID terbalik | Dikoreksi + arah via config (Bagian 9) |
| **A2/A3** | Fractional disederhanakan; campur satuan | Dicatat; dipertahankan, kandidat lanjutan (Bagian 9/16) |
| **Q1–Q5** | Dead code, magic number, LED, penamaan | Fase 6 (Bagian 15) + `config.h` (Bagian 14) |

---

*Dokumen hidup — perbarui saat keputusan desain berubah.*
