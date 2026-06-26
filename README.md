# Sistem Kontrol Suhu Biji Kopi

ESP32-based temperature control system for coffee bean roasting/drying. Menggunakan **Fuzzy Inference System (FIS)** + **Fractional Order PID (FoPID)** hybrid control dengan dual actuator: **dimmer AC** (blower) dan **servo** (katup gas).

---

## Fitur

- **Hybrid Control**: Fuzzy Logic + FoPID dengan anti-windup dan decay integral
- **Dual Actuator**: Dimmer AC (RBDdimmer) untuk blower + Servo untuk katup gas
- **Sensor**: MLX90614 (infrared suhu), DS3231 RTC, PZEM-004T (power meter)
- **Encoder**: Quadrature 500 PPR untuk monitoring RPM dryer
- **Display**: LCD 20x4 I2C (0x27) dengan navigasi keypad 4x4 (0x20)
- **Data Logging**: SD Card CSV dengan timestamp RTC
- **Web Dashboard**: ESPAsyncWebServer + WebSocket real-time (Chart.js)
- **mDNS**: Akses via `http://kopi.local`
- **Countdown Timer**: 10-200 menit dengan auto-stop

---

## Hardware (BOM)

| Komponen | Spesifikasi | Qty |
|----------|-------------|-----|
| ESP32 DevKit | ESP32-WROOM-32, dual-core 240MHz | 1 |
| MLX90614 | I2C Infrared Temp Sensor (0x5A) | 1 |
| DS3231 RTC | Real Time Clock + EEPROM (0x68, 0x57) | 1 |
| LCD 20x4 I2C | PCF8574 backpack (0x27) | 1 |
| Keypad 4x4 I2C | PCF8574 (0x20) | 1 |
| PZEM-004T v3.0 | Power Meter UART | 1 |
| Dimmer AC | RBDdimmer (zero-cross + gate) | 1 |
| Servo Motor | MG996R 180° | 1 |
| Encoder | Quadrature 500 PPR | 1 |
| SD Card Module | SPI | 1 |
| Power Supply | 5V 3A + 12V 2A | 1 |

---

## Pin Mapping

| Komponen | Pin ESP32 | Protocol |
|----------|-----------|----------|
| **I2C Bus** | SDA=21, SCL=22 | LCD, Keypad, MLX, RTC |
| **SD Card** | SCK=18, MISO=19, MOSI=23, CS=5 | SPI |
| **PZEM-004T** | RX=16, TX=17 | UART (9600) |
| **Dimmer Gate** | 32 | Digital |
| **Dimmer Zero-Cross** | 35 | Digital |
| **Servo PWM** | 13 | PWM (ESP32Servo) |
| **Encoder A** | 25 | Interrupt |
| **Encoder B** | 33 | Digital |
| **LED Indicator** | 12 | PWM (2kHz) |

### Wiring Diagram

```
                  ┌──────────────┐
                  │    ESP32     │
                  │              │
  ┌───────────────┤ SDA:21       ├───────────────┐
  │               │ SCL:22       │               │
  │  ┌────────────┤              │              │ │
  │  │ LCD 20x4   │              │   MLX90614   │ │
  │  │ (0x27)     │              │   (0x5A)     │ │
  │  │            │              │              │ │
  │  │ Keypad 4x4 │              │   DS3231 RTC │ │
  │  │ (0x20)     │              │   (0x68)     │ │
  │  └────────────┘              │              │ │
  │                 ┌────────────┤              │ │
  │                 │ Gate:32    │              │ │
  │                 │ Zero:35    │  Dimmer AC   │ │
  │                 │            │  → Blower    │ │
  │                 │ Servo:13   │              │ │
  │                 │            │  MG996R      │ │
  │                 │ EnA:25     │              │ │
  │                 │ EnB:33     │  Encoder     │ │
  │                 │            │  (RPM)       │ │
  │                 │ RX:16      │              │ │
  │                 │ TX:17      │  PZEM-004T   │ │
  │                 │ CS:5       │              │ │
  │                 │ MOSI:23    │  SD Card     │ │
  │                 │ MISO:19    │              │ │
  │                 │ SCK:18     │              │ │
  └─────────────────┴────────────┴──────────────┘
```

---

## Software Setup

### 1. Arduino IDE

**Board settings:**
```
Board: ESP32 Dev Module
Upload Speed: 921600
CPU Freq: 240MHz (WiFi/BT)
Flash Size: 4MB (32Mb)
Partition Scheme: Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)
```

### 2. Libraries

Install via Library Manager:

```
LiquidCrystal I2C          — LCD 20x4
Adafruit MLX90614           — Sensor suhu
RTClib                      — DS3231 RTC
PZEM004Tv30                 — Power meter
ESP32Servo                  — Servo motor
ESP Async WebServer         — Web server
ArduinoJson                 — JSON (v7+)
```

Library manual (dari GitHub):
- **I2CKeyPad** — Keypad 4x4 I2C
- **RBDdimmer** — AC dimmer

### 3. Upload Firmware

```
Sketch → Upload
```

### 4. Upload Data Web (LittleFS)

```
Tools → ESP32 LittleFS Data Upload
```

Atau PlatformIO:
```bash
pio run --target upload && pio run --target uploadfs
```

---

## Cara Penggunaan

### Navigasi Keypad

```
┌───┬───┬───┬───┐
│ 1 │ 2 │ 3 │ A │  A = Up / Previous
├───┼───┼───┼───┤
│ 4 │ 5 │ 6 │ B │  B = Down / Next
├───┼───┼───┼───┤
│ 7 │ 8 │ 9 │ C │  C = Enter / Confirm
├───┼───┼───┼───┤
│ * │ 0 │ # │ D │  D = Back / Cancel
└───┴───┴───┴───┘
```

### Langkah Operasi

1. **Startup**: LCD tampil splash → "v19+WS: Ready"
2. **Pilih Mode**: `Run Mode` → `FoPID+Fuzzy`
3. **Set Servo**: Masukkan sudut servo (0-180°) → `C` set, `D` lanjut
4. **Auto-Record**: Logging otomatis ke SD card + countdown
5. **Monitor**: Tekan `C` untuk ganti halaman (suhu, RPM, debug PID)
6. **Selesai**: Countdown habis → blower mati, servo tutup, log tersimpan

### Web Dashboard

Buka browser di perangkat同一 jaringan:

```
http://kopi.local
```

Atau pakai IP (cek Serial Monitor).

**Fitur:**
- Grafik suhu real-time (Chart.js, update 500ms)
- Kontrol setpoint dan servo
- Mode Fuzzy start / EMERGENCY STOP
- Download file log CSV dari SD Card

---

## Sistem Kontrol

### Hybrid: Fuzzy + FoPID

```
Error °C ──►┌──────────┐             ┌──────────┐
             │ FIS      │──► 30-75% ──┤          │
             │ (5 MF)   │             │ Dimmer   │──► Blower
Error %  ──►├──────────┤             │ Raw      │
             │ FoPID    │──► ±15 ──►  │ (clamp)  │
             │ (frac.)  │   ×β=0.4   └──────────┘
             └──────────┘
```

**FIS (Fuzzy Inference System):**
- Input 1: Error suhu (°C), range [-10, 40], 5 MF segitiga
- Input 2: Delta error, range [0, 5], 3 MF
- Output: Kecepatan blower (%), range [30, 75], 5 MF
- Defuzzifikasi: Centroid, 15 rule

**FoPID (Fractional Order PID):**
- Kp=0.60, Ki=0.08, Kd=0.50
- Lambda (integral) = 0.90, Mu (derivative) = 0.92
- Anti-windup: integral clip ±40, decay 0.96x saat <2°C dari SP
- Beta blower = 0.40 (bobot koreksi FoPID ke dimmer)

### Safety

| Kondisi | Aksi |
|---------|------|
| Sensor error (T<0 atau T>150°C) | Emergency stop |
| Over-temperature (>120°C) | Emergency stop |
| RPM > 39 atau < 23 | Warning/Sesuaikan |
| WiFi disconnect | Auto-reconnect + AP fallback |
| Power loss | NVS menyimpan sudut servo |

---

## API Web Server

| Endpoint | Method | Deskripsi |
|----------|--------|-----------|
| `/` | GET | Dashboard HTML |
| `/api/status` | GET | JSON status terkini |
| `/api/setpoint` | POST | Set suhu (body: `value=60.0`) |
| `/api/servo` | POST | Set servo (body: `angle=90`) |
| `/api/stop` | POST | Emergency stop |
| `/api/logs` | GET | Daftar file CSV di SD |
| `/api/download?file=...` | GET | Download file CSV |
| `/ws` | WebSocket | Real-time JSON stream |

### WebSocket JSON Format (500ms)

```json
{
  "temp": 58.5,
  "setpoint": 60.0,
  "error": 1.5,
  "blower": 64,
  "servo": 90,
  "rpm": 32.5,
  "power": 1500,
  "mode": "FUZZY",
  "fisOut": 63.5,
  "u_fopid": 0.45
}
```

---

## Parameter Tuning

### Via LCD Menu

`Set Params` → `Fuzzy & FoPID`:

| Parameter | Default | Range | Fungsi |
|-----------|---------|-------|--------|
| Kp | 0.60 | 0-5 | Proportional gain |
| Ki | 0.08 | 0-1 | Integral gain |
| Kd | 0.50 | 0-3 | Derivative gain |
| Lambda | 0.90 | 0-1 | Fractional integral order |
| Mu | 0.92 | 0-1 | Fractional derivative order |
| Beta | 0.40 | 0-1 | FoPID blending factor |
| SetPoint | 60.0 | 30-120 | Target suhu (°C) |

### Countdown Duration

Min: 10 menit, Max: 200 menit (default: 75)

---

## File Structure

```
├── ESP32_Firmware/
│   ├── ESP32_Firmware.ino     # Main firmware
│   ├── Fis_Header.h           # Fuzzy Inference System (v19)
│   └── data/
│       ├── index.html         # Web dashboard
│       ├── app.js             # WebSocket + Chart.js client
│       └── style.css          # Dashboard styling
├── Fis_Header_v19.h           # Backup FIS header
├── FoPID_Fuzzy_Blower_v19.ino # Backup main sketch
├── README.md
└── .gitignore
```

---

## Troubleshooting

| Masalah | Solusi |
|---------|--------|
| LCD tidak tampil | Cek alamat I2C (0x27), pull-up resistor 4.7kΩ |
| Sensor MLX error | Pastikan kabel terhubung, cek alamat (0x5A) |
| Web tidak bisa diakses | Pastikan satu jaringan WiFi, coba `http://192.168.x.x` |
| Kompilasi error `esp_intr.h` | Update RBDdimmer library (patch untuk ESP32 Core v3.x) |
| LED tidak menyala | Cek pin 12, pastikan PWM channel OK |
| Servo tidak bergerak | Cek power supply 5V, pin 13 |

---

## License

MIT
