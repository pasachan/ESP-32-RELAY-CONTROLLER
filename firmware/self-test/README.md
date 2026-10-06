# Hardware self-test

Bring-up firmware for a freshly assembled board. It exercises every fitted
component and prints a pass/fail report over USB serial.

No libraries are needed beyond the Arduino-ESP32 core — the WS2812B is driven
by the core's built-in `neopixelWrite()`, so there is no NeoPixel or FastLED
dependency.

---

## Build and flash

With [PlatformIO](https://platformio.org/):

```bash
cd firmware/self-test
pio run -t upload -t monitor
```

Or open `self-test.ino` in the Arduino IDE, select **ESP32 Dev Module**, set
flash size to **16 MB (128 Mb)**, and upload.

Either way the serial monitor runs at **115200 baud**.

> **Upload speed.** `platformio.ini` sets **1500000**, which flashes the 300 kB
> image in 1.6 s. Note that **921600 fails on this board while 1500000 works** —
> it is not a speed limit. See [below](#finding-the-ch340c-cannot-express-921600).

---

## What happens when you flash it

Nothing to type and no serial command to remember. On boot the board:

1. Flashes the status LED red, green, blue and beeps once — proof of life for
   someone watching the board rather than a terminal. If you see and hear that,
   the 5 V rail, the WS2812B and the buzzer are all alive.
2. Runs the passive checks (below) and prints them.
3. Counts down five seconds, blinking the LED once a second, then **starts the
   full sweep by itself**.

Press any key during the countdown to get the menu instead and drive the tests
one at a time. At the end the report asks you to confirm what you saw; if
nobody answers within 30 s it stops waiting, marks those checks `??` and prints
the report anyway, so the sketch is usable with no terminal attached at all.

> **It energises all eight relays on every boot.** Fine on a bare board, which
> is what it is for — but don't leave this firmware on a board wired to loads
> you care about.

---

## Applying power

Apply **12 V to J1** before running the relay, buzzer or LED tests.

USB VBUS is diode-ORed into the 3.3 V regulator only, so the MCU, the CH340C
and the MAX3485 all run happily from the USB cable alone — but the 5 V rail
that feeds the relay coils, the buzzer and the WS2812B does not. On USB alone
those three tests run to completion and report success while nothing physically
happens. That is by design, not a fault.

---

## Passive checks

These have no side effects, so they run before the countdown, every boot:

| Check | What a pass proves |
|---|---|
| 3.3 V rail, MCU alive | U3 regulating, module running, crystal ok |
| USB-C, CH340C, UART0 | Connector, ESD array, bridge, GPIO1/3 routing, auto-reset |
| MAX3485 RO idles high | Transceiver powered, RO routed to GPIO16, and the 330 Ω fail-safe bias pair (R26/R27) fitted the right way round |
| Input 1 (J7) reads open | The 10 k pull-up on GPIO34 is fitted |
| User button reads released | The 10 k pull-down on GPIO35 is fitted |
| I²C bus scan | SDA and SCL are not shorted to each other or to a rail |

A `FAIL` on the RO check means the transceiver is unpowered, the bias resistors
are missing, or A and B are swapped or shorted.

---

## The menu

Press any key during the boot countdown to land here instead of running the
sweep. Everything that moves, sounds or lights up is also on the menu
individually, because only a human can confirm that a relay actually clicked.

```
1  WS2812B status LED          6  input monitor (live)
2  buzzer                      7  RS-485 send / listen
3  relays one at a time        8  I2C scan
4  all relays on (2 s)         r  print report
5  relay chase                 0  everything off
a  run every test in order     ?  this menu
```

Any key aborts a running test. `0` forces every output off.

**Test 3** is the one worth running carefully. It walks the bank one relay at a
time and prints the silkscreen number, screw terminal, relay designator,
ULN2803A channel and GPIO for each, so you can follow it on the board. The
silkscreen order is not the GPIO order and not the designator order, so this is
what actually validates the mapping end to end:

| Silk | Terminal | Relay | ULN | GPIO |
|---|---|---|---|---|
| 1 | J3 | K2 | IN2 | 14 |
| 2 | J9 | K5 | IN6 | 19 |
| 3 | J11 | K4 | IN8 | 25 |
| 4 | J2 | K9 | IN1 | 13 |
| 5 | J10 | K3 | IN7 | 23 |
| 6 | J6 | K8 | IN4 | 33 |
| 7 | J4 | K6 | IN3 | 32 |
| 8 | J8 | K7 | IN5 | 18 |

**Test 4** energises all eight coils at once: 8 × 71.4 mA = 571 mA off the 5 V
rail and about 0.5 W in the ULN2803A. The LMR51430 is rated 3 A, so this is
comfortable — it is there to catch a weak rail or a marginal coil, not to
stress anything. If the board resets, the report will say `BROWNOUT` as the
reset reason next time it boots.

**Test 7** sends five frames at 9600 8N1 and then listens for five seconds.
With nothing attached to J12, "nothing received" is the expected result.

The LED is left as a standing verdict after a report: dim green for all pass,
dim amber if something was never run, dim red for any failure.

---

## Bring-up results, rev 1

First assembled board, 6 Oct 2026. ESP32-D0WD rev 1.1, 16 MB flash.

```
 [PASS] 3.3 V rail / MCU alive     you are reading this
 [PASS] USB-C + CH340C + UART0     this serial link
 [PASS] RS-485 receiver idle       RO high, bias ok   (20/20 samples)
 [PASS] Input 1 (J7) readable      pull-up ok, open
 [PASS] User button (SW3)          pull-down ok
 [PASS] I2C bus scan               bus idle, nothing fitted
 [PASS] WS2812B status LED         operator confirmed
 [PASS] Buzzer                     operator confirmed
 [PASS] Relays 1-8 individually    operator confirmed
 [PASS] Relays all on (5 V load)   operator confirmed
```

Everything fitted on the board works. One firmware-level finding came out of
the exercise, described below.

---

## Finding: RO floats during transmit

The first version of test 7 reported ~5 bytes of nonsense received per
transmitted frame, with **nothing attached to J12 and JP1 open**. With no node
on the bus, those bytes could only be self-generated.

It is not the unterminated stub, and not a truncated turn-around. DE and RE̅
are tied together on GPIO2, so enabling the driver also *disables the
receiver* — and a disabled MAX3485 drives RO to **high impedance**. There is no
pull-up on RO in rev 1, so GPIO16 floats for the entire duration of every
transmission (~19 ms for an 18-byte frame at 9600 baud) and the UART clocks in
phantom bytes from the crosstalk off the adjacent DI line.

Lengthening the mark hold before dropping DE changed nothing — 26 bytes before,
26 after. Enabling the ESP32's internal pull-up on GPIO16 took it to **zero**:

```c
Serial2.begin(9600, SERIAL_8N1, RS485_RX_GPIO, RS485_TX_GPIO);
gpio_set_pull_mode((gpio_num_t)RS485_RX_GPIO, GPIO_PULLUP_ONLY);
```

`Serial2.begin()` leaves the RX pin with no pull, so this has to be set after
it. Any firmware for this board wants those two lines together; without them a
receive buffer fills with garbage on every turn-around and a protocol parser
has to resynchronise constantly.

The hardware fix for rev 2 is a 10 kΩ pull-up from RO to 3.3 V, which is what
most RS-485 reference designs fit for exactly this reason. It is in the
[rev 2 backlog](../../docs/design-notes.md#rev-2-backlog).

---

## Finding: the CH340C cannot express 921600

`pio run -t upload` at 921600 dies right after the baud change:

```
Changing baud rate to 921600
Changed.
A fatal error occurred: Unable to verify flash chip connection
  (Invalid head of packet (0xE0): Possible serial noise or corruption.)
```

"Serial noise" is esptool guessing, and it is wrong. Sweeping the rate shows
the failures are not above a threshold — they are at *particular* rates:

| Requested | n | Actual | Error | Result |
|---|---|---|---|---|
| 115200 | 52 | 115385 | +0.16 % | ok |
| 230400 | 26 | 230769 | +0.16 % | ok |
| 460800 | 13 | 461538 | +0.16 % | ok |
| 576000 | 10 | 600000 | **+4.17 %** | **fails** |
| 600000 | 10 | 600000 | 0.00 % | ok |
| 750000 | 8 | 750000 | 0.00 % | ok |
| 921600 | 7 | 857143 | **−6.99 %** | **fails** |
| 1000000 | 6 | 1000000 | 0.00 % | ok |
| 1200000 | 5 | 1200000 | 0.00 % | ok |
| 1500000 | 4 | 1500000 | 0.00 % | ok |

750000 works and 576000 does not. 1500000 works and 921600 does not. Nothing
about that is a signal-integrity ceiling.

The CH340C derives its baud rate by dividing a 48 MHz clock through a
prescaler and an 8-bit divisor. The measured set of working rates is exactly
**6 000 000 / n** for integer n, which is what you get if the host driver uses
only the coarse prescaler setting. This board enumerates as
`/dev/cu.usbserial-*`, i.e. Apple's built-in CH34x driver — WCH's own driver
names the device `wchusbserial` and may well divide more finely. The 6 MHz/n
set is therefore a property of this host, not of the board, and the table
above is worth re-measuring on Linux or with the vendor driver.

Either way the mechanism is plain: 921600 is unreachable, the driver lands on
857143 instead, and a receiver running 7 % slow samples the last data bit and
the stop bit in the wrong bit cell. An 8N1 UART tolerates roughly ±2 %. That
is also why the corrupted byte was `0xE0`: SLIP frames start with `0xC0`
(`11000000`), and `0xE0` is `11100000` — the same byte with the sampling point
slipped one cell into the idle-high line.

Practical rule for this board: pick a rate that divides 6 MHz exactly.
1500000 is the fastest that works and flashes the 300 kB image in 1.6 s,
against 4.3 s at 460800 — both hash-verified.

---

## Licence

`SPDX-License-Identifier: CERN-OHL-P-2.0` — see [`LICENSE`](../../LICENSE).
