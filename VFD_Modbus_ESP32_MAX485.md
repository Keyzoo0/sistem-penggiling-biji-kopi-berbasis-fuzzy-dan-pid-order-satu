# VFD MCU-T13 Modbus RTU Control — ESP32 + MAX485

## Wiring

```
MAX485 DE+RE  →  ESP32 GPIO25
MAX485 RO     →  ESP32 GPIO26  (Serial2 RX)
MAX485 DI     →  ESP32 GPIO27  (Serial2 TX)
MAX485 A      →  RS485+ VFD   (terminal pin 3)
MAX485 B      →  RS485- VFD   (terminal pin 4)
MAX485 VCC    →  3.3V / 5V
MAX485 GND    →  GND
```

> **Catatan:** DE dan RE MAX485 dijumper jadi satu ke GPIO25. HIGH = transmit, LOW = receive.

---

## Setting Parameter VFD via Panel (WAJIB sebelum pakai Modbus)

| Parameter | Nilai | Keterangan |
|---|---|---|
| `-1.0-` | `3` | Sumber frekuensi dari RS485 |
| `-1.1-` | `1` | Start/stop dikendalikan RS485 |
| `-0.7-` | `1` | Baud rate 9600 |
| `-0.8-` | `1` | Format 8N1 |
| `-0.9-` | `1` | Slave address = 1 |

**Cara save:** Setelah semua diset, tekan **K3 (SAVE)** dua kali sampai display berhenti kedip.

---

## Register Modbus

> Register di bawah adalah standar umum VFD China. Manual MCU-T13 tidak mencantumkan register secara eksplisit — jika tidak response, coba juga `0x0001`/`0x0002` atau cek extended manual dari vendor.

| Register | Alamat | Mode | Keterangan |
|---|---|---|---|
| Control Word | `0x2000` | Write | Run / Stop / Arah |
| Set Frequency | `0x2001` | Write | Unit: 0.01 Hz (50 Hz = 5000) |
| Status Word | `0x3000` | Read | Status VFD |

### Control Word Values

| Value | Fungsi |
|---|---|
| `0x0001` | Run forward |
| `0x0003` | Run reverse |
| `0x0005` | Stop |
| `0x0007` | Reset fault |

---

## Kode Program (ESP32 Arduino)

```cpp
// ============================================================
// VFD MCU-T13 Modbus RTU Control — ESP32 + MAX485
// ============================================================

#define RS485_DE_RE   25
#define RS485_RX      26    // Serial2 RX
#define RS485_TX      27    // Serial2 TX

#define VFD_SLAVE_ID  0x01
#define BAUD_RATE     9600

#define REG_CONTROL   0x2000
#define REG_FREQUENCY 0x2001
#define REG_STATUS    0x3000

#define CMD_RUN_FWD   0x0001
#define CMD_RUN_REV   0x0003
#define CMD_STOP      0x0005
#define CMD_RESET     0x0007

#define TX_DELAY_MS   2
#define RX_TIMEOUT_MS 100

// ---- Prototypes ----
void setTxMode();
void setRxMode();
int readResponse(uint8_t* buf, int expectedLen);
uint16_t calcCRC16(uint8_t* data, int len);
bool writeRegister(uint8_t slaveId, uint16_t regAddr, uint16_t value);
bool readRegister(uint8_t slaveId, uint16_t regAddr, uint16_t* outValue);

// ============================================================
void setup() {
  Serial.begin(115200);
  pinMode(RS485_DE_RE, OUTPUT);
  setRxMode();
  Serial2.begin(BAUD_RATE, SERIAL_8N1, RS485_RX, RS485_TX);
  delay(500);

  Serial.println("=== VFD Modbus RTU Controller ===");
  Serial.println("  r       = Run forward");
  Serial.println("  b       = Run reverse");
  Serial.println("  s       = Stop");
  Serial.println("  f<Hz>   = Set frekuensi (contoh: f30.5)");
  Serial.println("  q       = Query status");
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    handleCommand(input);
  }
  delay(10);
}

// ============================================================
// Command Handler
// ============================================================
void handleCommand(String cmd) {
  if (cmd.length() == 0) return;
  char first = cmd.charAt(0);

  if      (first == 'r') { Serial.println(">> RUN FORWARD"); vfdRun(true); }
  else if (first == 'b') { Serial.println(">> RUN REVERSE"); vfdRunReverse(); }
  else if (first == 's') { Serial.println(">> STOP");        vfdStop(); }
  else if (first == 'q') { Serial.println(">> QUERY STATUS"); vfdReadStatus(); }
  else if (first == 'f') {
    float freq = cmd.substring(1).toFloat();
    if (freq >= 0.0 && freq <= 50.0) {
      Serial.printf(">> SET FREQ: %.1f Hz\n", freq);
      vfdSetFrequency(freq);
    } else {
      Serial.println("!! Frekuensi harus 0.0 - 50.0 Hz");
    }
  }
}

// ============================================================
// VFD High-Level Functions
// ============================================================
void vfdRun(bool forward) {
  writeRegister(VFD_SLAVE_ID, REG_CONTROL, forward ? CMD_RUN_FWD : CMD_RUN_REV);
}

void vfdRunReverse() {
  writeRegister(VFD_SLAVE_ID, REG_CONTROL, CMD_RUN_REV);
}

void vfdStop() {
  writeRegister(VFD_SLAVE_ID, REG_CONTROL, CMD_STOP);
}

void vfdSetFrequency(float hz) {
  uint16_t freqVal = (uint16_t)(hz * 100);  // unit: 0.01 Hz
  writeRegister(VFD_SLAVE_ID, REG_FREQUENCY, freqVal);
}

void vfdReadStatus() {
  uint16_t statusVal = 0;
  bool ok = readRegister(VFD_SLAVE_ID, REG_STATUS, &statusVal);
  if (ok) {
    Serial.printf("   Status: 0x%04X\n", statusVal);
    if (statusVal & 0x0001) Serial.println("   [Running]");
    if (statusVal & 0x0002) Serial.println("   [Fault]");
    if (statusVal & 0x0004) Serial.println("   [Forward]");
    if (statusVal & 0x0008) Serial.println("   [Reverse]");
  } else {
    Serial.println("   Gagal baca status — cek wiring/parameter VFD");
  }
}

// ============================================================
// Modbus RTU — Write Single Register (FC 0x06)
// ============================================================
bool writeRegister(uint8_t slaveId, uint16_t regAddr, uint16_t value) {
  uint8_t frame[8];
  frame[0] = slaveId;
  frame[1] = 0x06;
  frame[2] = (regAddr >> 8) & 0xFF;
  frame[3] = regAddr & 0xFF;
  frame[4] = (value >> 8) & 0xFF;
  frame[5] = value & 0xFF;
  uint16_t crc = calcCRC16(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;

  setTxMode();
  delay(TX_DELAY_MS);
  Serial2.write(frame, 8);
  Serial2.flush();
  setRxMode();

  Serial.print("   TX: ");
  for (int i = 0; i < 8; i++) Serial.printf("%02X ", frame[i]);
  Serial.println();

  uint8_t resp[8];
  int len = readResponse(resp, 8);
  if (len == 8 && resp[0] == slaveId && resp[1] == 0x06) {
    Serial.println("   OK");
    return true;
  }
  Serial.printf("   WARN: response len=%d\n", len);
  return false;
}

// ============================================================
// Modbus RTU — Read Holding Register (FC 0x03)
// ============================================================
bool readRegister(uint8_t slaveId, uint16_t regAddr, uint16_t* outValue) {
  uint8_t frame[8];
  frame[0] = slaveId;
  frame[1] = 0x03;
  frame[2] = (regAddr >> 8) & 0xFF;
  frame[3] = regAddr & 0xFF;
  frame[4] = 0x00;
  frame[5] = 0x01;
  uint16_t crc = calcCRC16(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;

  setTxMode();
  delay(TX_DELAY_MS);
  Serial2.write(frame, 8);
  Serial2.flush();
  setRxMode();

  uint8_t resp[7];
  int len = readResponse(resp, 7);
  if (len == 7 && resp[0] == slaveId && resp[1] == 0x03 && resp[2] == 0x02) {
    *outValue = ((uint16_t)resp[3] << 8) | resp[4];
    return true;
  }
  return false;
}

// ============================================================
// Helper
// ============================================================
void setTxMode() { digitalWrite(RS485_DE_RE, HIGH); }
void setRxMode()  { digitalWrite(RS485_DE_RE, LOW);  }

int readResponse(uint8_t* buf, int expectedLen) {
  unsigned long start = millis();
  int idx = 0;
  while (millis() - start < RX_TIMEOUT_MS) {
    if (Serial2.available()) {
      buf[idx++] = Serial2.read();
      if (idx >= expectedLen) break;
    }
  }
  return idx;
}

uint16_t calcCRC16(uint8_t* data, int len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i];
    for (int j = 0; j < 8; j++) {
      crc = (crc & 0x0001) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}
```

---

## Cara Pakai via Serial Monitor

| Input | Aksi |
|---|---|
| `r` | Run forward |
| `b` | Run reverse |
| `s` | Stop |
| `f30` | Set frekuensi 30 Hz |
| `f50` | Set frekuensi 50 Hz |
| `f0` | Set frekuensi 0 Hz |
| `q` | Baca status register VFD |

**Urutan yang disarankan:**
1. Set frekuensi dulu → `f30`
2. Jalankan motor → `r`
3. Stop motor → `s`

---

## Troubleshooting

| Gejala | Kemungkinan Penyebab | Solusi |
|---|---|---|
| Tidak ada response (len=0) | Wiring A/B terbalik | Tukar kabel A↔B |
| Response tapi VFD tidak bergerak | Parameter `-1.0-` atau `-1.1-` belum di-set ke RS485 | Set ulang via panel |
| Error E-0.2 di VFD | Overload / waktu akselerasi terlalu cepat | Naikkan nilai `-0.1-` (acceleration time) |
| TX frame keluar tapi tidak ada echo | DE tidak HIGH saat transmit | Cek koneksi GPIO25 ke DE+RE |
| Frekuensi tidak berubah | Register address beda | Coba `0x0002` sebagai `REG_FREQUENCY` |

---

## Modbus Frame Reference

### Write Single Register (FC 06) — Contoh Run Forward

```
TX: 01 06 20 00 00 01 43 CA
     |  |  |-----|  |-----|  |---------|
   Slave FC  Reg   Value    CRC16
```

### Write Frequency 30 Hz (= 3000 = 0x0BB8)

```
TX: 01 06 20 01 0B B8 XX XX
                     ↑
              3000 = 30.00 Hz
```

---

## Catatan Register Alternatif

Jika register `0x2000`/`0x2001` tidak bekerja, coba kombinasi ini (umum di VFD China lain):

| Vendor | Control | Frequency |
|---|---|---|
| Standar (default program ini) | `0x2000` | `0x2001` |
| Alternatif 1 | `0x0001` | `0x0002` |
| Alternatif 2 | `0xF000` | `0xF001` |

Lihat output debug TX/RX di Serial Monitor untuk analisis response.
