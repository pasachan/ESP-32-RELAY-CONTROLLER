/*
 * ESP32 Relay Controller - hardware self-test
 * ===========================================
 *
 * Bring-up firmware for a freshly assembled rev 1 board. It exercises every
 * fitted component and prints a pass/fail report over the USB serial port.
 *
 *   Serial:  115200 baud, 8N1, over the on-board CH340C (USB-C)
 *
 * Checks that need no operator and no side effects run automatically at boot.
 * Anything that moves, sounds or lights up is on the menu, because only a
 * human can confirm a relay actually clicked.
 *
 * ---------------------------------------------------------------------------
 * POWER: apply 12 V to J1 before running the relay, buzzer or LED tests.
 * ---------------------------------------------------------------------------
 * USB VBUS is diode-ORed into the 3.3 V regulator only, so the MCU, CH340C and
 * MAX3485 all run from the USB cable - but the 5 V rail that feeds the relay
 * coils, the buzzer and the WS2812B does not. On USB alone those three tests
 * run and report success while nothing physically happens. That is by design,
 * not a fault. See docs/design-notes.md.
 *
 * Dependencies: none beyond the Arduino-ESP32 core (>= 2.0.3, for the built-in
 * neopixelWrite). No NeoPixel or FastLED library needed.
 *
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */

#include <Arduino.h>
#include <Wire.h>
#include <driver/gpio.h>

/* ========================================================================
 * Pin map - rev 1. Mirrors docs/pinout.md; change both together.
 * ======================================================================== */

#define RELAY_COUNT 8

/* Dwell times for the relay walk. Long enough to watch a contact move and
 * hear it as a separate event, rather than as a blur of clicks. */
static const uint16_t RELAY_ON_MS  = 1200;
static const uint16_t RELAY_OFF_MS =  450;
static const uint16_t CHASE_MS     =  280;

/* Relay coil drives in SILKSCREEN order 1-8, active high into a ULN2803A.
 * The silkscreen order is not the GPIO order and not the relay-designator
 * order, so the test prints all three and you check the board against it. */
static const uint8_t RELAY_GPIO[RELAY_COUNT]   = {   14,   19,   25,   13,   23,   33,   32,   18 };
static const char *const RELAY_REF[RELAY_COUNT]  = { "K2", "K5", "K4", "K9", "K3", "K8", "K6", "K7" };
static const char *const RELAY_TERM[RELAY_COUNT] = { "J3", "J9", "J11", "J2", "J10", "J6", "J4", "J8" };
static const uint8_t RELAY_ULN_IN[RELAY_COUNT] = {    2,    6,    8,    1,    7,    4,    3,    5 };

#define BUZZER_GPIO       26   /* SS8050 NPN, 4k7 base resistor. High = on.   */
#define STATUS_LED_GPIO   27   /* WS2812B DIN through 330R, one pixel         */

#define RS485_TX_GPIO     17   /* -> MAX3485 DI                               */
#define RS485_RX_GPIO     16   /* <- MAX3485 RO                               */
#define RS485_DIR_GPIO     2   /* DE and /RE tied. High = transmit.           */
                               /* Strapping pin: external 10k pull-down keeps */
                               /* it low at boot. Never add an internal pull. */

#define INPUT1_GPIO       34   /* J7 dry contact to GND, ext 10k pull-up.     */
                               /* Closed = LOW. Input-only pin.               */
#define USER_BTN_GPIO     35   /* SW3 "WPS" to 3V3, ext 10k pull-down.        */
                               /* Pressed = HIGH. Input-only pin.             */
#define BOOT_BTN_GPIO      0   /* SW2 "BOOT" to GND, 10k pull-up.             */
                               /* Pressed = LOW.                              */

#define I2C_SDA_GPIO      21   /* Routed, but no pull-ups fitted in rev 1     */
#define I2C_SCL_GPIO      22

/* ========================================================================
 * Test bookkeeping
 * ======================================================================== */

enum Result : uint8_t { R_SKIP = 0, R_PASS, R_FAIL, R_MANUAL };

struct Check {
  const char *name;
  Result      result;
  const char *note;
};

static Check checks[] = {
  { "3.3 V rail / MCU alive",    R_PASS, "you are reading this" },
  { "USB-C + CH340C + UART0",    R_PASS, "this serial link"     },
  { "RS-485 receiver idle",      R_SKIP, ""                     },
  { "Input 1 (J7) readable",     R_SKIP, ""                     },
  { "User button (SW3)",         R_SKIP, ""                     },
  { "I2C bus scan",              R_SKIP, ""                     },
  { "WS2812B status LED",        R_SKIP, "operator confirms"    },
  { "Buzzer",                    R_SKIP, "operator confirms"    },
  { "Relays 1-8 individually",   R_SKIP, "operator confirms"    },
  { "Relays all on (5 V load)",  R_SKIP, "operator confirms"    },
};

enum CheckIdx : uint8_t {
  CHK_RAIL = 0, CHK_USB, CHK_RS485_IDLE, CHK_INPUT1, CHK_USERBTN,
  CHK_I2C, CHK_LED, CHK_BUZZER, CHK_RELAY_EACH, CHK_RELAY_ALL
};

static void mark(CheckIdx i, Result r, const char *note = "") {
  checks[i].result = r;
  if (note[0]) checks[i].note = note;
}

/* ========================================================================
 * Small helpers
 * ======================================================================== */

static bool askYesNo();   /* defined under "Serial UI" below */

/* Tests that need a human to confirm them ask directly when run on their own,
 * but stay quiet during the full sweep - stopping for a y/n between every
 * stage defeats the point of watching the board as a whole. The sweep asks
 * once at the end instead. */
static bool g_askOperator = true;

static void confirm(CheckIdx idx, const char *question) {
  if (!g_askOperator) { mark(idx, R_MANUAL, "ran in full sweep"); return; }
  Serial.printf("   %s [y/n]\n", question);
  mark(idx, askYesNo() ? R_PASS : R_FAIL, "operator confirmed");
}

/* Any key during a long test aborts it and drops back to the menu. */
static bool aborted() {
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    Serial.println("\n  [aborted]");
    return true;
  }
  return false;
}

/* delay() that can be cut short by a keypress. Returns false if aborted. */
static bool waitMs(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (aborted()) return false;
    delay(2);
  }
  return true;
}

static void ledOff()                                { neopixelWrite(STATUS_LED_GPIO, 0, 0, 0); }
static void led(uint8_t r, uint8_t g, uint8_t b)     { neopixelWrite(STATUS_LED_GPIO, r, g, b); }

/* Hue 0-255 -> RGB at full saturation. Enough for a rainbow sweep. */
static void hueToRgb(uint8_t h, uint8_t &r, uint8_t &g, uint8_t &b) {
  uint8_t seg = h / 43;            /* 6 segments of 42.6 */
  uint8_t off = (h - seg * 43) * 6;
  switch (seg) {
    case 0:  r = 255;       g = off;       b = 0;         break;
    case 1:  r = 255 - off; g = 255;       b = 0;         break;
    case 2:  r = 0;         g = 255;       b = off;       break;
    case 3:  r = 0;         g = 255 - off; b = 255;       break;
    case 4:  r = off;       g = 0;         b = 255;       break;
    default: r = 255;       g = 0;         b = 255 - off; break;
  }
}

static void relayWrite(uint8_t idx, bool on) {
  digitalWrite(RELAY_GPIO[idx], on ? HIGH : LOW);
}

static void relaysAllOff() {
  for (uint8_t i = 0; i < RELAY_COUNT; i++) relayWrite(i, false);
}

/* Everything to a known-safe state. Called at boot and by the '0' command. */
static void allOutputsOff() {
  relaysAllOff();
  digitalWrite(BUZZER_GPIO, LOW);
  ledOff();
}

/* ========================================================================
 * Passive checks - no side effects, safe to run unattended at boot
 * ======================================================================== */

/* The MAX3485 sits in receive mode at boot (GPIO2 pulled down). The board's
 * fail-safe bias - 330R from 3.3 V to A, 330R from B to GND - holds the idle
 * differential at ~508 mV with one terminator fitted, well over the 200 mV
 * receiver threshold. So RO must sit HIGH. A LOW here means the transceiver
 * is unpowered, the bias resistors are missing, or A/B are swapped or shorted.
 *
 * Read the pin raw, before UART2 claims it. */
static void checkRs485Idle() {
  pinMode(RS485_DIR_GPIO, OUTPUT);
  digitalWrite(RS485_DIR_GPIO, LOW);          /* receive */
  pinMode(RS485_RX_GPIO, INPUT);
  delay(5);

  uint8_t high = 0;
  for (uint8_t i = 0; i < 20; i++) { high += digitalRead(RS485_RX_GPIO); delay(1); }

  Serial.printf("  MAX3485 RO idle ....... %s  (%u/20 samples high)\n",
                high >= 18 ? "HIGH - ok" : "LOW  - CHECK", high);
  if (high >= 18) mark(CHK_RS485_IDLE, R_PASS, "RO high, bias ok");
  else            mark(CHK_RS485_IDLE, R_FAIL, "RO low - transceiver, bias or A/B wiring");
}

static void checkInputs() {
  pinMode(INPUT1_GPIO,   INPUT);   /* no internal pulls on 34/35 anyway */
  pinMode(USER_BTN_GPIO, INPUT);
  pinMode(BOOT_BTN_GPIO, INPUT);
  delay(5);

  bool in1  = digitalRead(INPUT1_GPIO);
  bool user = digitalRead(USER_BTN_GPIO);
  bool boot = digitalRead(BOOT_BTN_GPIO);

  Serial.printf("  Input 1 (J7) .......... %s  (%s)\n",
                in1 ? "HIGH" : "LOW ", in1 ? "contact open" : "contact closed");
  Serial.printf("  User button (SW3) ..... %s  (%s)\n",
                user ? "HIGH" : "LOW ", user ? "pressed" : "released");
  Serial.printf("  BOOT button (SW2) ..... %s  (%s)\n",
                boot ? "HIGH" : "LOW ", boot ? "released" : "pressed");

  /* An idle board should read: J7 high (10k pull-up, nothing connected),
   * SW3 low (10k pull-down), SW2 high (10k pull-up). All three stuck the
   * wrong way with nothing attached points at a missing resistor. */
  mark(CHK_INPUT1,  in1  ? R_PASS : R_MANUAL, in1  ? "pull-up ok, open" : "reads closed");
  mark(CHK_USERBTN, user ? R_MANUAL : R_PASS, user ? "reads pressed"    : "pull-down ok");
}

/* Rev 1 fits no I2C device - the SHT4x was dropped to make room for the
 * RS-485 section - so finding nothing is the expected result, not a failure.
 * The scan is here for when a sensor is added, and it does prove the two
 * pins are not shorted to each other or to a rail. */
static void checkI2c() {
  Wire.begin(I2C_SDA_GPIO, I2C_SCL_GPIO, 100000);
  uint8_t found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  I2C device found ...... 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  I2C scan .............. no devices (expected on rev 1)");
    mark(CHK_I2C, R_PASS, "bus idle, nothing fitted");
  } else {
    mark(CHK_I2C, R_PASS, "device(s) responded");
  }
}

/* ========================================================================
 * Active tests - these make noise and move contacts
 * ======================================================================== */

static void testLed() {
  Serial.println("\n-- WS2812B status LED (D15) ------------------------------");
  Serial.println("   Expect: red, green, blue, white, then a rainbow sweep.");
  Serial.println("   Wrong colour order means the pixel is not GRB.");

  struct { const char *name; uint8_t r, g, b; } steps[] = {
    { "red",   255,   0,   0 },
    { "green",   0, 255,   0 },
    { "blue",    0,   0, 255 },
    { "white", 255, 255, 255 },
  };

  for (auto &s : steps) {
    Serial.printf("   %-6s", s.name);
    Serial.flush();
    led(s.r, s.g, s.b);
    if (!waitMs(700)) { ledOff(); return; }
    Serial.println(" ok");
  }

  Serial.print("   rainbow");
  Serial.flush();
  for (uint16_t h = 0; h < 256; h += 2) {
    uint8_t r, g, b;
    hueToRgb(h, r, g, b);
    led(r, g, b);
    if (!waitMs(12)) { ledOff(); return; }
  }
  Serial.println(" ok");
  ledOff();

  confirm(CHK_LED, "Did you see all four colours and the sweep?");
}

static void testBuzzer() {
  Serial.println("\n-- Buzzer (LS1) ------------------------------------------");
  Serial.println("   Expect: three short beeps, then one long.");

  for (uint8_t i = 0; i < 3; i++) {
    digitalWrite(BUZZER_GPIO, HIGH);
    if (!waitMs(90))  { digitalWrite(BUZZER_GPIO, LOW); return; }
    digitalWrite(BUZZER_GPIO, LOW);
    if (!waitMs(160)) return;
  }
  if (!waitMs(250)) return;
  digitalWrite(BUZZER_GPIO, HIGH);
  if (!waitMs(500)) { digitalWrite(BUZZER_GPIO, LOW); return; }
  digitalWrite(BUZZER_GPIO, LOW);

  confirm(CHK_BUZZER, "Did you hear it?");
}

/* Walk the bank one relay at a time. This is the test that proves the whole
 * silkscreen -> terminal -> relay -> ULN channel -> GPIO mapping, so it
 * prints every one of those and you follow along on the board. */
static void testRelaysIndividually() {
  Serial.println("\n-- Relays, one at a time ---------------------------------");
  Serial.println("   Needs 12 V on J1. Listen for eight clicks in order.");
  Serial.println();
  Serial.println("   Silk  Terminal  Relay  ULN    GPIO");
  Serial.println("   ----  --------  -----  -----  ----");

  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    Serial.printf("   %-4u  %-8s  %-5s  IN%-3u  %-4u",
                  i + 1, RELAY_TERM[i], RELAY_REF[i], RELAY_ULN_IN[i], RELAY_GPIO[i]);
    Serial.flush();

    relayWrite(i, true);
    if (!waitMs(RELAY_ON_MS)) { relaysAllOff(); return; }
    relayWrite(i, false);
    Serial.println("  click");
    if (!waitMs(RELAY_OFF_MS)) { relaysAllOff(); return; }
  }
  relaysAllOff();

  Serial.println();
  confirm(CHK_RELAY_EACH, "Eight clicks, in silkscreen order 1 to 8?");
}

/* All eight coils at once: 8 x 71.4 mA = 571 mA off the 5 V rail, and about
 * 0.5 W in the ULN2803A. The LMR51430 is rated 3 A, so this is comfortable -
 * it is here to catch a weak rail or a marginal coil, not to stress anything.
 * Two seconds is long enough to see a droop and short enough to stay cool. */
static void testRelaysAllOn() {
  Serial.println("\n-- All relays on together --------------------------------");
  Serial.println("   571 mA on the 5 V rail for 2 s. ULN gets warm; that is normal.");

  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    relayWrite(i, true);
    delay(25);                      /* stagger slightly to soften inrush */
  }
  Serial.println("   all energised");
  if (!waitMs(2000)) { relaysAllOff(); return; }
  relaysAllOff();
  Serial.println("   all released");

  Serial.println();
  confirm(CHK_RELAY_ALL, "Did all eight hold without the board resetting?");
}

static void testRelayChase() {
  Serial.println("\n-- Relay chase (any key stops) ---------------------------");
  uint8_t i = 0;
  for (;;) {
    relaysAllOff();
    relayWrite(i, true);
    uint8_t r, g, b;
    hueToRgb((uint8_t)(i * 32), r, g, b);
    led(r / 8, g / 8, b / 8);       /* dim, so it is not blinding */
    if (!waitMs(CHASE_MS)) break;
    i = (i + 1) % RELAY_COUNT;
  }
  relaysAllOff();
  ledOff();
}

/* Everything at once: two laps of the relay bank with the status LED tracking
 * the active channel and a beep at the end of each lap. This is the stage that
 * shows the 5 V rail carrying a coil, the buzzer and the WS2812B together
 * rather than one at a time. */
static void testFinale() {
  Serial.println("\n-- All components together -------------------------------");
  Serial.println("   Two laps: each relay in turn, LED follows, beep per lap.");

  for (uint8_t lap = 0; lap < 2; lap++) {
    Serial.printf("   lap %u:", lap + 1);
    Serial.flush();

    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
      relaysAllOff();
      relayWrite(i, true);
      uint8_t r, g, b;
      hueToRgb((uint8_t)(i * 32), r, g, b);
      led(r / 4, g / 4, b / 4);
      Serial.printf(" %u", i + 1);
      Serial.flush();
      if (!waitMs(CHASE_MS)) { allOutputsOff(); return; }
    }

    relaysAllOff();
    ledOff();
    digitalWrite(BUZZER_GPIO, HIGH);
    if (!waitMs(120)) { allOutputsOff(); return; }
    digitalWrite(BUZZER_GPIO, LOW);
    Serial.println(" beep");
    if (!waitMs(350)) { allOutputsOff(); return; }
  }
  allOutputsOff();
}

/* Live edge monitor. The only way to test a dry contact and a button is to
 * have someone press them. */
static void testInputsLive() {
  Serial.println("\n-- Input monitor (any key stops) -------------------------");
  Serial.println("   Short J7 pin 1 to pin 2, and press SW3 / SW2.");

  bool in1 = digitalRead(INPUT1_GPIO);
  bool usr = digitalRead(USER_BTN_GPIO);
  bool bt  = digitalRead(BOOT_BTN_GPIO);
  bool sawIn1 = false, sawUsr = false;

  for (;;) {
    bool n1 = digitalRead(INPUT1_GPIO);
    bool nu = digitalRead(USER_BTN_GPIO);
    bool nb = digitalRead(BOOT_BTN_GPIO);

    if (n1 != in1) {
      Serial.printf("   [%7lu ms] Input 1 (J7)   -> %s\n",
                    millis(), n1 ? "OPEN" : "CLOSED");
      in1 = n1; sawIn1 = true;
      led(0, n1 ? 0 : 40, 0);
    }
    if (nu != usr) {
      Serial.printf("   [%7lu ms] SW3 user btn   -> %s\n",
                    millis(), nu ? "PRESSED" : "released");
      usr = nu; sawUsr = true;
      led(0, 0, nu ? 40 : 0);
    }
    if (nb != bt) {
      Serial.printf("   [%7lu ms] SW2 BOOT btn   -> %s\n",
                    millis(), nb ? "released" : "PRESSED");
      bt = nb;
    }
    if (aborted()) break;
    delay(8);
  }
  ledOff();
  if (sawIn1) mark(CHK_INPUT1,  R_PASS, "toggled under test");
  if (sawUsr) mark(CHK_USERBTN, R_PASS, "pressed under test");
}

/* Half-duplex turn-around. Raise DE, write, wait for the shift register to
 * empty, hold the line a moment, then drop DE - dropping it early truncates
 * the last byte, which is the classic RS-485 bug. Serial2.flush() on the
 * ESP32 blocks until the transmitter is idle, which is what we need here.
 *
 * Disabling the driver then lets A/B relax from driven levels to the much
 * weaker fail-safe bias levels. With JP1 unfitted the stub is unterminated,
 * so that settle is slow, and because DE and /RE are tied together our own
 * receiver switches back on partway through it and reads a few bytes of
 * nonsense. That is the transceiver behaving normally, not a board fault. A
 * real protocol discards it on the CRC; this test counts and bins it, so the
 * echo display below only shows traffic that genuinely came from elsewhere. */
static void testRs485() {
  Serial.println("\n-- RS-485 (J12) ------------------------------------------");
  Serial.println("   Sending 5 frames at 9600 8N1. Watch A/B on a scope, or");
  Serial.println("   hook up a USB-RS485 dongle. Fit JP1 only at a bus end.");

  Serial2.begin(9600, SERIAL_8N1, RS485_RX_GPIO, RS485_TX_GPIO);

  /* DE and /RE are tied together, so enabling the driver also disables the
   * receiver - and a disabled MAX3485 drives RO to high impedance. There is
   * no pull-up on RO in rev 1, so GPIO16 floats for the whole of every
   * transmission and the UART clocks in phantom bytes from the crosstalk.
   * The ESP32's internal pull-up holds it in the idle mark instead. Set it
   * after begin(), which otherwise leaves the pin with no pull. */
  gpio_set_pull_mode((gpio_num_t)RS485_RX_GPIO, GPIO_PULLUP_ONLY);
  delay(20);

  uint16_t binned = 0;
  for (uint8_t i = 0; i < 5; i++) {
    digitalWrite(RS485_DIR_GPIO, HIGH);        /* drive */
    delayMicroseconds(100);                    /* driver enable time */
    Serial2.printf("SELFTEST frame %u\r\n", i + 1);
    Serial2.flush();                           /* transmitter idle */
    delayMicroseconds(1000);                   /* hold the idle mark */
    digitalWrite(RS485_DIR_GPIO, LOW);         /* back to receive */

    delay(3);                                  /* let A/B settle to bias */
    while (Serial2.available()) { Serial2.read(); binned++; }

    Serial.printf("   sent frame %u\n", i + 1);
    if (!waitMs(200)) break;
  }
  if (binned) Serial.printf("   binned %u byte(s) of turn-around noise\n", binned);

  Serial.println("   Listening for 5 s - anything received is echoed below.");
  uint32_t t0 = millis();
  uint16_t rx = 0;
  while (millis() - t0 < 5000) {
    while (Serial2.available()) {
      char c = (char)Serial2.read();
      if (rx == 0) Serial.print("   rx: ");
      Serial.print(c >= 32 && c < 127 ? c : '.');
      rx++;
    }
    if (aborted()) break;
    delay(5);
  }
  if (rx) Serial.printf("\n   %u byte(s) received from the bus\n", rx);
  else    Serial.println("   nothing received (expected with J12 empty)");
}

/* ========================================================================
 * Serial UI
 * ======================================================================== */

static bool askYesNo() {
  while (Serial.available()) Serial.read();
  for (;;) {
    if (Serial.available()) {
      char c = (char)Serial.read();
      if (c == 'y' || c == 'Y') { Serial.println("   -> PASS"); return true;  }
      if (c == 'n' || c == 'N') { Serial.println("   -> FAIL"); return false; }
    }
    delay(10);
  }
}

static const char *resultStr(Result r) {
  switch (r) {
    case R_PASS:   return "PASS";
    case R_FAIL:   return "FAIL";
    case R_MANUAL: return "??  ";
    default:     return "--  ";
  }
}

static void printReport() {
  Serial.println("\n=========================================================");
  Serial.println(" Self-test report");
  Serial.println("=========================================================");
  uint8_t pass = 0, fail = 0, skip = 0;
  for (auto &c : checks) {
    Serial.printf(" [%s] %-26s %s\n", resultStr(c.result), c.name, c.note);
    if      (c.result == R_PASS) pass++;
    else if (c.result == R_FAIL) fail++;
    else                       skip++;
  }
  Serial.printf("\n %u passed, %u failed, %u not run\n", pass, fail, skip);
  Serial.println("=========================================================");

  /* Leave the LED as a standing verdict: dim green all good, dim red a fault,
   * dim amber if something was never run. */
  if (fail)      led(30, 0, 0);
  else if (skip) led(24, 12, 0);
  else           led(0, 30, 0);
}

static void printBanner() {
  Serial.println();
  Serial.println("=========================================================");
  Serial.println(" ESP32 Relay Controller - hardware self-test");
  Serial.println("=========================================================");
  Serial.printf(" Chip ........... %s rev %u, %u core(s) @ %u MHz\n",
                ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getChipCores(), getCpuFrequencyMhz());
  Serial.printf(" Flash .......... %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf(" Free heap ...... %u bytes\n", ESP.getFreeHeap());

  uint64_t mac = ESP.getEfuseMac();
  uint8_t  m[6];
  for (uint8_t i = 0; i < 6; i++) m[i] = (uint8_t)(mac >> (8 * i));
  Serial.printf(" MAC ............ %02X:%02X:%02X:%02X:%02X:%02X\n",
                m[0], m[1], m[2], m[3], m[4], m[5]);

  const char *rr;
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  rr = "power-on";            break;
    case ESP_RST_EXT:      rr = "external / RESET";    break;
    case ESP_RST_SW:       rr = "software";            break;
    case ESP_RST_PANIC:    rr = "panic";               break;
    case ESP_RST_INT_WDT:  rr = "interrupt watchdog";  break;
    case ESP_RST_TASK_WDT: rr = "task watchdog";       break;
    case ESP_RST_WDT:      rr = "other watchdog";      break;
    case ESP_RST_BROWNOUT: rr = "BROWNOUT - weak rail"; break;
    default:               rr = "unknown";             break;
  }
  Serial.printf(" Reset reason ... %s\n", rr);
  Serial.printf(" Arduino core ... %d.%d.%d\n",
                ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR,
                ESP_ARDUINO_VERSION_PATCH);
  Serial.println("=========================================================");
}

static void printMenu() {
  Serial.println("\n Commands");
  Serial.println("   1  WS2812B status LED          6  input monitor (live)");
  Serial.println("   2  buzzer                      7  RS-485 send / listen");
  Serial.println("   3  relays one at a time        8  I2C scan");
  Serial.println("   4  all relays on (2 s)         9  all components together");
  Serial.println("   5  relay chase                 a  full sweep (everything)");
  Serial.println("   0  everything off              r  print report / ?  menu");
  Serial.print("\n > ");
}

static void runAll() {
  Serial.println("\n#########################################################");
  Serial.println(" FULL SWEEP - runs straight through. Any key aborts a stage.");
  Serial.println("#########################################################");

  g_askOperator = false;            /* no y/n between stages */
  testLed();
  testBuzzer();
  testRelaysIndividually();
  testRelaysAllOn();
  testFinale();
  testRs485();
  g_askOperator = true;

  allOutputsOff();
  Serial.println("\n-- Sweep complete ---------------------------------------");
  Serial.println("   Did every stage behave as described above? [y/n]");
  Result r = askYesNo() ? R_PASS : R_FAIL;
  mark(CHK_LED,        r, "full sweep");
  mark(CHK_BUZZER,     r, "full sweep");
  mark(CHK_RELAY_EACH, r, "full sweep");
  mark(CHK_RELAY_ALL,  r, "full sweep");
  printReport();
}

/* ========================================================================
 * setup / loop
 * ======================================================================== */

void setup() {
  /* Drive the output register low BEFORE switching the pins to outputs, so
   * they never glitch high on the way. The ULN inputs read low while the
   * ESP32 is in reset, so the relays are already off - this keeps them off
   * across the pinMode calls too. */
  for (uint8_t i = 0; i < RELAY_COUNT; i++) {
    digitalWrite(RELAY_GPIO[i], LOW);
    pinMode(RELAY_GPIO[i], OUTPUT);
    digitalWrite(RELAY_GPIO[i], LOW);
  }
  digitalWrite(BUZZER_GPIO, LOW);
  pinMode(BUZZER_GPIO, OUTPUT);
  digitalWrite(BUZZER_GPIO, LOW);

  ledOff();

  Serial.begin(115200);
  delay(400);                      /* let the host open the port */

  printBanner();

  Serial.println("\n-- Passive checks ----------------------------------------");
  checkRs485Idle();
  checkInputs();
  checkI2c();

  Serial.println("\n NOTE: relays, buzzer and the WS2812B run from the 5 V rail,");
  Serial.println("       which is fed only from 12 V on J1 - not from USB. On USB");
  Serial.println("       alone those tests pass silently with nothing happening.");

  printMenu();
}

void loop() {
  if (!Serial.available()) { delay(10); return; }

  char c = (char)Serial.read();
  if (c == '\r' || c == '\n' || c == ' ') return;
  Serial.println(c);

  switch (c) {
    case '1': testLed();                break;
    case '2': testBuzzer();             break;
    case '3': testRelaysIndividually(); break;
    case '4': testRelaysAllOn();        break;
    case '5': testRelayChase();         break;
    case '6': testInputsLive();         break;
    case '7': testRs485();              break;
    case '8': checkI2c();               break;
    case '9': testFinale();             break;
    case 'a':
    case 'A': runAll();                 break;
    case 'r':
    case 'R': printReport();            break;
    case '0':
      allOutputsOff();
      Serial.println("   all outputs off");
      break;
    default:
      printMenu();
      return;
  }
  printMenu();
}
