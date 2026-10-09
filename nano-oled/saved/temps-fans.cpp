// Previous screen: temps on top, a fan row for GPU, CPU, and SYS below.
// Kept out of src/ so PlatformIO does not build it. Copy over src/main.cpp to restore.
#include <Arduino.h>
#include <U8g2lib.h>

// Filled by the host over serial: "gpuC,cpuC,sysC,gpuFan,cpuFan,sysFan".
// Fan numbers are percents and may read past 100 when a fan beats its max.
int gpuTemp = 0;
int cpuTemp = 0;
int sysTemp = 0;

int gpuFan = 0;
int cpuFan = 0;
int sysFan = 0;

// Portrait: 64 wide, 128 tall. RES is Nano D8.
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C display(U8G2_R1, /* reset=*/ 8);

static const char *kNames[] = {"GPU", "CPU", "SYS"};

// 12 steps of 30 degrees. Blades are shortened so they stay inside a square housing.
static const int8_t kBladeX[12] = {5, 4, 3, 0, -3, -4, -5, -4, -3, 0, 3, 4};
static const int8_t kBladeY[12] = {0, 3, 4, 5, 4, 3, 0, -3, -4, -5, -4, -3};

static uint8_t fanPhase[3];
static uint32_t fanCarryMs[3];
static uint32_t fanTickMs;

// Three identical blades repeat every 4 steps, so a bigger jump looks
// stopped or backwards. Two steps per redraw is as fast as the panel
// can show while still turning the right way.
static void tickFans() {
  uint32_t now = millis();
  uint32_t dt = now - fanTickMs;
  fanTickMs = now;
  if (dt > 500) {
    dt = 500;
  }

  int values[] = {gpuFan, cpuFan, sysFan};
  for (uint8_t i = 0; i < 3; i++) {
    int percent = values[i];
    if (percent <= 0) {
      continue;
    }
    // The digits can show 101 and up. The blades still top out at full
    // speed, or a bigger jump looks like the fan is turning backwards.
    if (percent > 100) {
      percent = 100;
    }

    uint8_t steps = 0;
    if (percent >= 75) {
      steps = 2;
    } else if (percent >= 25) {
      steps = 1;
    } else {
      uint32_t interval = 900 - (uint32_t)percent * 25;
      fanCarryMs[i] += dt;
      if (fanCarryMs[i] >= interval) {
        steps = 1;
        fanCarryMs[i] = 0;
      }
    }
    fanPhase[i] = (fanPhase[i] + steps) % 12;
  }
}

static void drawCorners(int x, int y, int w, int h) {
  const int len = 5;
  display.drawHLine(x, y, len);
  display.drawVLine(x, y, len);
  display.drawHLine(x + w - len, y, len);
  display.drawVLine(x + w - 1, y, len);
  display.drawHLine(x, y + h - 1, len);
  display.drawVLine(x, y + h - len, len);
  display.drawHLine(x + w - len, y + h - 1, len);
  display.drawVLine(x + w - 1, y + h - len, len);
}

static void drawTag(const char *text, int x, int baseline) {
  display.setFont(u8g2_font_profont11_tr);
  int w = display.getStrWidth(text);
  display.drawBox(x, baseline - 9, w + 3, 11);
  display.setDrawColor(0);
  display.drawStr(x + 1, baseline, text);
  display.setDrawColor(1);
}

static void drawFan(int x, int y, uint8_t step) {
  display.drawFrame(x, y, 13, 13);
  int cx = x + 6;
  int cy = y + 6;
  for (uint8_t blade = 0; blade < 3; blade++) {
    uint8_t i = (step + blade * 4) % 12;
    int tx = cx + kBladeX[i];
    int ty = cy + kBladeY[i];
    display.drawLine(cx, cy, tx, ty);
    if (blade == 0) {
      display.drawDisc(tx, ty, 1);
    }
  }
  display.drawBox(cx - 1, cy - 1, 3, 3);
}

static void drawTemps() {
  int values[] = {gpuTemp, cpuTemp, sysTemp};
  display.setFont(u8g2_font_tom_thumb_4x6_tr);
  display.drawStr(4, 7, "TEMP");
  for (int x = 22; x < 60; x += 2) {
    display.drawPixel(x, 5);
  }

  for (uint8_t i = 0; i < 3; i++) {
    int baseline = 27 + i * 17;
    drawTag(kNames[i], 1, baseline - 2);

    display.setFont(u8g2_font_logisoso16_tn);
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", values[i]);
    int w = display.getStrWidth(buf);
    int nx = 56 - w;
    display.drawStr(nx, baseline, buf);
    display.drawCircle(nx + w + 3, baseline - 13, 1);
  }
}

static void drawFans() {
  int values[] = {gpuFan, cpuFan, sysFan};
  display.setFont(u8g2_font_tom_thumb_4x6_tr);
  display.drawStr(4, 71, "FAN");
  for (int x = 18; x < 60; x += 2) {
    display.drawPixel(x, 69);
  }

  for (uint8_t i = 0; i < 3; i++) {
    int top = 76 + i * 17;
    drawFan(1, top, fanPhase[i]);

    display.setFont(u8g2_font_profont11_tr);
    display.drawStr(16, top + 6, kNames[i]);

    display.setFont(u8g2_font_profont12_tr);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", values[i]);
    int w = display.getStrWidth(buf);
    display.drawStr(64 - w, top + 11, buf);
  }
}

static const uint32_t kStaleMs = 5000;
static char line[32];
static uint8_t lineLen;
static uint32_t lastFrameMs;
static bool haveFrame;
static bool panelLit;

static bool parseFrame(const char *s) {
  int values[6];
  for (uint8_t i = 0; i < 6; i++) {
    if (*s == '\0') {
      return false;
    }
    bool neg = false;
    if (*s == '-') {
      neg = true;
      s++;
    }
    if (*s < '0' || *s > '9') {
      return false;
    }
    int value = 0;
    while (*s >= '0' && *s <= '9') {
      value = value * 10 + (*s - '0');
      if (value > 999) {
        return false;
      }
      s++;
    }
    values[i] = neg ? -value : value;
    if (i < 5) {
      if (*s != ',') {
        return false;
      }
      s++;
    }
  }
  if (*s != '\0') {
    return false;
  }
  gpuTemp = values[0];
  cpuTemp = values[1];
  sysTemp = values[2];
  gpuFan = values[3];
  cpuFan = values[4];
  sysFan = values[5];
  lastFrameMs = millis();
  haveFrame = true;
  return true;
}

static void pollSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') {
      continue;
    }
    if (c != '\n') {
      if (lineLen < sizeof(line) - 1) {
        line[lineLen++] = c;
      } else {
        lineLen = 0;
      }
      continue;
    }
    line[lineLen] = '\0';
    if (lineLen > 0) {
      parseFrame(line);
    }
    lineLen = 0;
  }
}

void setup() {
  Serial.begin(115200);
  display.setBusClock(400000);
  display.setI2CAddress(0x3C * 2);
  display.begin();
  display.setContrast(255);
}

void loop() {
  pollSerial();
  bool live = haveFrame && (millis() - lastFrameMs) < kStaleMs;
  if (!live) {
    if (panelLit) {
      panelLit = false;
      display.clearBuffer();
      display.sendBuffer();
    }
    return;
  }
  panelLit = true;
  tickFans();
  display.clearBuffer();
  drawCorners(0, 0, 64, 128);
  drawTemps();
  drawFans();

  // Sweep a short mark along the split so the panel doesn't sit still.
  uint8_t scan = (millis() / 30) % 64;
  display.drawBox(scan, 63, 4, 1);
  display.sendBuffer();
}
