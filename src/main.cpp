#include <Arduino.h>
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>

#define CRSF_TX_PIN 17
#define CRSF_RX_PIN 16
#define CRSF_BAUD   416666
#define SEND_MS     10          // 100 Hz

XboxSeriesXControllerESP32_asukiaaa::Core xbox;

// ---------------- state ----------------
float thr = 0;                 // 0..1 after the cap is applied
float thrCap = 0.30f;          // throttle limiter, D-pad Up/Down in 10% steps
bool armed = false;
uint8_t modeIdx = 0;
const uint16_t modeUs[6] = {1100, 1295, 1425, 1555, 1685, 1900};
bool lastA = false, lastLB = false, lastRB = false, lastUp = false, lastDown = false;
uint32_t lastSend = 0;

// ---------------- debug counters ----------------
uint32_t framesSent = 0, rxBytes = 0, rxFrames = 0, rxBadCrc = 0;
uint8_t  lastRxType = 0, rxBuf[64], rxLen = 0;

// ---------------- CRSF ----------------
static uint8_t crc8(const uint8_t *p, uint8_t len) {
  uint8_t crc = 0;
  while (len--) {
    crc ^= *p++;
    for (uint8_t i = 0; i < 8; i++) crc = (crc & 0x80) ? (crc << 1) ^ 0xD5 : (crc << 1);
  }
  return crc;
}

// microseconds (1000-2000) -> CRSF ticks (172-1811, center 992)
static uint16_t usToCrsf(float us) {
  us = constrain(us, 988.0f, 2012.0f);
  return (uint16_t)((us - 1500.0f) * 1.6f + 992.0f);
}

static void sendCrsf(const uint16_t ch[16]) {
  uint8_t f[26];
  f[0] = 0xC8; f[1] = 24; f[2] = 0x16;       // sync, length, RC_CHANNELS_PACKED
  uint32_t buf = 0; uint8_t bits = 0, idx = 3;
  for (int i = 0; i < 16; i++) {
    buf |= (uint32_t)(ch[i] & 0x7FF) << bits;
    bits += 11;
    while (bits >= 8) { f[idx++] = buf & 0xFF; buf >>= 8; bits -= 8; }
  }
  f[25] = crc8(&f[2], 23);
  Serial2.write(f, 26);
  framesSent++;
}

// Parses CRSF frames arriving on RX (18): FC telemetry, or your own frames in loopback
void pollCrsfRx() {
  while (Serial2.available()) {
    uint8_t b = Serial2.read(); rxBytes++;
    if (rxLen == 0 && b != 0xC8 && b != 0xEA && b != 0xEE && b != 0xEC) continue;
    rxBuf[rxLen++] = b;
    if (rxLen == 2 && (rxBuf[1] < 2 || rxBuf[1] > 62)) { rxLen = 0; continue; }
    if (rxLen >= 2 && rxLen == rxBuf[1] + 2) {
      if (crc8(&rxBuf[2], rxBuf[1] - 1) == rxBuf[rxLen - 1]) { rxFrames++; lastRxType = rxBuf[2]; }
      else rxBadCrc++;
      rxLen = 0;
    }
  }
}

// raw 0..65535 -> -1..+1 with deadband
static float axis(uint16_t raw) {
  float v = ((float)raw - 32768.0f) / 32768.0f;
  return fabsf(v) < 0.06f ? 0.0f : v;
}

// 1 Hz status line + connect/disconnect events
void printStatus() {
  static uint32_t t = 0, prevSent = 0;
  static bool wasConn = false;
  bool conn = xbox.isConnected();
  if (conn != wasConn) {
    wasConn = conn;
    Serial.println(conn ? "[EVT] Xbox CONNECTED" : "[EVT] Xbox DISCONNECTED -> CRSF stopped");
  }
  if (millis() - t < 1000) return;
  t = millis();
  Serial.printf("[%lus] BLE:%s fails:%d | TX %lu fps (total %lu) | RX bytes %lu frames %lu badCRC %lu lastType 0x%02X | thr=%.2f cap=%d%% arm=%d mode=%d\n",
                millis() / 1000,
                conn ? "OK" : (xbox.isWaitingForFirstNotification() ? "WAIT" : "SCAN"),
                xbox.getCountFailedConnection(),
                framesSent - prevSent, framesSent,
                rxBytes, rxFrames, rxBadCrc, lastRxType,
                thr, (int)(thrCap * 100 + 0.5f), armed, modeIdx);
  prevSent = framesSent;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(CRSF_BAUD, SERIAL_8N1, CRSF_RX_PIN, CRSF_TX_PIN);
  xbox.begin();
  Serial.println("[BOOT] Xbox -> CRSF bridge started (A=arm, throttle cap 30%)");
}

void loop() {
  xbox.onLoop();
  pollCrsfRx();
  printStatus();

  if (!xbox.isConnected()) {            // no link -> send nothing -> FC RC failsafe
    thr = 0; armed = false;
    return;
  }
  if (millis() - lastSend < SEND_MS) return;
  lastSend = millis();

  auto &n = xbox.xboxNotif;
  float lx = axis(n.joyLHori), ly = axis(n.joyLVert);
  float rx = axis(n.joyRHori), ry = axis(n.joyRVert);

  // Spring-return throttle: stick up (negative on HID Y) OR right trigger. Center/released = 0.
  // If stick-up reads as throttle DOWN in Radio Calibration, change -ly to ly below.
  float stickUp = (-ly > 0.0f) ? -ly : 0.0f;
  float trig    = n.trigRT / 1023.0f;                 // trigger range 0..1023
  thr = constrain(fmaxf(stickUp, trig), 0.0f, 1.0f) * thrCap;

  // B = emergency stop: zero throttle and disarm
  if (n.btnB) {
    thr = 0;
    if (armed) { armed = false; Serial.println("[EVT] B pressed -> throttle 0, DISARM"); }
  }

  // A = arm/disarm toggle
  if (n.btnA && !lastA) { armed = !armed; Serial.printf("[EVT] arm switch -> %d\n", armed); }

  // D-pad Up/Down = throttle cap in 10% steps (10%..100%)
  if (n.btnDirUp && !lastUp && thrCap < 0.99f) {
    thrCap = fminf(1.0f, thrCap + 0.10f);
    Serial.printf("[EVT] throttle cap -> %d%%\n", (int)(thrCap * 100 + 0.5f));
  }
  if (n.btnDirDown && !lastDown && thrCap > 0.11f) {
    thrCap = fmaxf(0.10f, thrCap - 0.10f);
    Serial.printf("[EVT] throttle cap -> %d%%\n", (int)(thrCap * 100 + 0.5f));
  }

  // LB / RB = flight mode down / up
  if (n.btnRB && !lastRB && modeIdx < 5) { modeIdx++; Serial.printf("[EVT] mode -> %d\n", modeIdx); }
  if (n.btnLB && !lastLB && modeIdx > 0) { modeIdx--; Serial.printf("[EVT] mode -> %d\n", modeIdx); }

  lastA = n.btnA; lastRB = n.btnRB; lastLB = n.btnLB;
  lastUp = n.btnDirUp; lastDown = n.btnDirDown;

  uint16_t ch[16];
  for (int i = 0; i < 16; i++) ch[i] = usToCrsf(1500);
  ch[0] = usToCrsf(1500 + rx * 500);          // CH1 roll
  ch[1] = usToCrsf(1500 + ry * 500);          // CH2 pitch (stick forward = low, as ArduPilot expects)
  ch[2] = usToCrsf(1000 + thr * 1000);        // CH3 throttle
  ch[3] = usToCrsf(1500 + lx * 500);          // CH4 yaw
  ch[4] = usToCrsf(modeUs[modeIdx]);          // CH5 flight mode
  ch[5] = usToCrsf(armed ? 1900 : 1100);      // CH6 arm
  ch[6] = usToCrsf(n.btnX ? 2000 : 1000);     // CH7 aux, X held = high
  ch[7] = usToCrsf(n.btnY ? 2000 : 1000);     // CH8 aux, Y held = high
  ch[8] = usToCrsf(n.btnDirLeft  ? 2000 : 1000);   // CH9 aux, D-pad Left held = high
  ch[9] = usToCrsf(n.btnDirRight ? 2000 : 1000);   // CH10 aux, D-pad Right held = high
  sendCrsf(ch);
}