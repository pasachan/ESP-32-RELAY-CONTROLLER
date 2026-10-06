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

> **Upload speed.** `platformio.ini` sets 460800. 921600 fails on this board
> with `Invalid head of packet` partway through the handshake — the CH340C and
> the USB-C stub do not reliably carry it. 460800 flashes the 300 kB image in
> about four seconds.

---

## Applying power

Apply **12 V to J1** before running the relay, buzzer or LED tests.

USB VBUS is diode-ORed into the 3.3 V regulator only, so the MCU, the CH340C
and the MAX3485 all run happily from the USB cable alone — but the 5 V rail
that feeds the relay coils, the buzzer and the WS2812B does not. On USB alone
those three tests run to completion and report success while nothing physically
happens. That is by design, not a fault.

---

## What runs by itself

These have no side effects, so they run automatically at boot:

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

Anything that moves, sounds or lights up is on the menu, because only a human
can confirm that a relay actually clicked.

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

## Licence

`SPDX-License-Identifier: CERN-OHL-P-2.0` — see [`LICENSE`](../../LICENSE).
