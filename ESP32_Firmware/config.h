// =============================================================================
//  config.h — Konstanta global (pin, ambang, arah kontrol, default, jaringan)
//  Satu-satunya tempat "angka ajaib". Lihat ARCHITECTURE.md Bagian 14.
// =============================================================================
#ifndef CONFIG_H
#define CONFIG_H

// ── Pin ──────────────────────────────────────────────────────────────────────
#define PIN_SDA        21
#define PIN_SCL        22
#define PIN_DIMMER     32     // gate dimmer AC (blower)
#define PIN_ZEROCROSS  35
#define PIN_SERVO      13     // katup gas
#define PIN_LED        12     // indikator status
#define PIN_ENC_A      25
#define PIN_ENC_B      33
#define PIN_PZEM_RX    16
#define PIN_PZEM_TX    17
#define PIN_SD_CS       5     // SPI: SCK18 MISO19 MOSI23

// ── Alamat I2C ───────────────────────────────────────────────────────────────
#define ADDR_LCD       0x27
#define ADDR_KEYPAD    0x20

// ── Aktuator ─────────────────────────────────────────────────────────────────
#define DIMMER_MIN      0
#define DIMMER_MAX    100     // kurva v2: pendinginan bisa sampai 100%
#define SERVO_MAX      180

// ── Model plant / arah kontrol (lihat ARCHITECTURE.md Bagian 3 & 9) ──────────
//  Blower bersifat dua arah: <pivot = memanaskan, >pivot = mendinginkan.
#define BLOWER_PIVOT_PCT  30
#define BLOWER_IS_COOLER  true   // true: di rentang kerja, blower besar = mendinginkan

// ── Safety SUHU (pemicu FAULT) ───────────────────────────────────────────────
#define SAFE_MAX_TEMP    120.0f   // over-temp → FAULT
#define SENSOR_MIN_TEMP    0.0f
#define SENSOR_MAX_TEMP  150.0f
#define SENSOR_TIMEOUT_MS 5000UL  // tak ada pembacaan valid selama ini → FAULT

// ── RPM (monitoring + warning saja, TIDAK memicu FAULT) ──────────────────────
#define RPM_WARN_LOW      23.0f
#define RPM_NORM_LOW      25.0f
#define RPM_NORM_HIGH     37.0f
#define RPM_WARN_HIGH     39.0f
#define RPM_GEAR_RATIO     8.0f
#define RPM_STARTUP_MS  30000UL
#define ENC_PPR          500
#define ENC_EDGES_PER_REV (ENC_PPR * 2)   // attachInterrupt CHANGE = 2 edge/pulse

// ── Kontrol — default (dapat diubah operator via UI/web) ─────────────────────
#define DEF_SETPOINT     60.0f
#define DEF_KP            0.20f   // FoPID = trim lembut di atas kurva FIS (tunable di web)
#define DEF_KI            0.03f
#define DEF_KD            0.05f
#define DEF_LAMBDA        0.90f
#define DEF_MU            0.92f
#define DEF_BETA          0.50f
#define DT_FIXED          0.5f

#define DEF_DURATION_MIN    75UL
#define DURATION_MIN_LIMIT  10UL
#define DURATION_MAX_LIMIT 200UL

#define DEF_FREQ_MOTOR    50.0f   // Hz — Wafi: kecepatan motor konstan (via VFD)
#define FREQ_MOTOR_MAX    50.0f

// FoPID clamp & decay
#define FOPID_U_CLAMP     14.0f
#define FOPID_I_CLAMP     60.0f
#define FOPID_D_CLAMP     40.0f

// Kurva v2: deadband di sekitar setpoint → blower = DEADBAND_BLOWER
#define SP_DEADBAND_PCT   1.0f    // ±% setpoint dianggap "tercapai"
#define DEADBAND_BLOWER   0       // 0 = mati (spec); set ~30 utk hold tanpa osilasi

// FIS input mapping (°C) — rentang error termasuk overshoot
#define FIS_ERR_MIN      -30.0f
#define FIS_ERR_MAX       40.0f

// ── Periode task (ms) ────────────────────────────────────────────────────────
#define RT_TICK_MS         100    // tick dasar realtime task
#define CONTROL_PERIOD_MS  500
#define ENC_PERIOD_MS      300
#define PZEM_PERIOD_MS    1000
#define UI_PERIOD_MS        80
#define LOG_PERIOD_MS     5000    // rekam data logger tiap 5 detik
#define WS_PERIOD_MS       500

// ── Jaringan ─────────────────────────────────────────────────────────────────
// Kredensial WiFi asli letakkan di secrets.h (di-gitignore). Lihat secrets.h.example.
#if __has_include("secrets.h")
  #include "secrets.h"
#endif
#ifndef WIFI_SSID
  #define WIFI_SSID   "GANTI_WIFI"
  #define WIFI_PASS   "GANTI_PASSWORD"
#endif
#define AP_SSID     "Kopi-Control"
#define AP_PASS     "12345678"
#define HOSTNAME    "kopi"

#endif // CONFIG_H
