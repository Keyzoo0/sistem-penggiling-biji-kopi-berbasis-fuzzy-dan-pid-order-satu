#include "vfd.h"
#include "config.h"

#define VFD_TX_DELAY_MS 2
#define VFD_RX_TIMEOUT_MS 60

static inline void setTx() { digitalWrite(PIN_RS485_DE_RE, HIGH); }
static inline void setRx() { digitalWrite(PIN_RS485_DE_RE, LOW);  }

static uint16_t calcCRC16(const uint8_t* data, int len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i];
    for (int j = 0; j < 8; j++)
      crc = (crc & 0x0001) ? (crc >> 1) ^ 0xA001 : crc >> 1;
  }
  return crc;
}

static int readResponse(uint8_t* buf, int expectedLen) {
  unsigned long start = millis();
  int idx = 0;
  while (millis() - start < VFD_RX_TIMEOUT_MS) {
    if (Serial1.available()) {
      buf[idx++] = Serial1.read();
      if (idx >= expectedLen) break;
    }
  }
  return idx;
}

void vfdInit() {
  pinMode(PIN_RS485_DE_RE, OUTPUT);
  setRx();
  Serial1.begin(VFD_BAUD, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
  Serial.println("[VFD] Modbus RTU siap (Serial1)");
}

// Write Single Register (FC 0x06)
static bool writeRegister(uint16_t regAddr, uint16_t value) {
  uint8_t f[8];
  f[0] = VFD_SLAVE_ID; f[1] = 0x06;
  f[2] = (regAddr >> 8) & 0xFF; f[3] = regAddr & 0xFF;
  f[4] = (value >> 8) & 0xFF;   f[5] = value & 0xFF;
  uint16_t crc = calcCRC16(f, 6);
  f[6] = crc & 0xFF; f[7] = (crc >> 8) & 0xFF;

  setTx(); delay(VFD_TX_DELAY_MS);
  Serial1.write(f, 8); Serial1.flush();
  setRx();

  uint8_t resp[8];
  int len = readResponse(resp, 8);
  return (len == 8 && resp[0] == VFD_SLAVE_ID && resp[1] == 0x06);
}

bool vfdSetFrequency(float hz) {
  if (hz < 0) hz = 0; if (hz > VFD_FREQ_MAX_HZ) hz = VFD_FREQ_MAX_HZ;
  return writeRegister(VFD_REG_FREQ, (uint16_t)(hz * 100.0f));   // unit 0.01 Hz
}
bool vfdRun()  { return writeRegister(VFD_REG_CONTROL, VFD_CMD_RUN_FWD); }
bool vfdStop() { return writeRegister(VFD_REG_CONTROL, VFD_CMD_STOP); }

// Read Holding Register (FC 0x03), 1 register
bool vfdReadStatus(uint16_t* outValue) {
  uint8_t f[8];
  f[0] = VFD_SLAVE_ID; f[1] = 0x03;
  f[2] = (VFD_REG_STATUS >> 8) & 0xFF; f[3] = VFD_REG_STATUS & 0xFF;
  f[4] = 0x00; f[5] = 0x01;
  uint16_t crc = calcCRC16(f, 6);
  f[6] = crc & 0xFF; f[7] = (crc >> 8) & 0xFF;

  setTx(); delay(VFD_TX_DELAY_MS);
  Serial1.write(f, 8); Serial1.flush();
  setRx();

  uint8_t resp[7];
  int len = readResponse(resp, 7);
  if (len == 7 && resp[0] == VFD_SLAVE_ID && resp[1] == 0x03 && resp[2] == 0x02) {
    *outValue = ((uint16_t)resp[3] << 8) | resp[4];
    return true;
  }
  return false;
}
