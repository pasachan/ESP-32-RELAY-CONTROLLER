# Pinout

Everything below is extracted from the rev 1 netlist. GPIO numbers are ESP32 GPIO numbers (not module pad numbers).

## ESP32 GPIO map

| GPIO | Net | Direction | Function | Notes |
|---|---|---|---|---|
| 0 | IO0 | in | BOOT | 10 k pull-up, BOOT button to GND, auto-reset transistor |
| 1 | TX | out | UART0 TXD | → CH340C RXD |
| 2 | Direction | out | RS-485 DE + RE̅ | **Strapping pin.** 10 k pull-down (R22) → receive mode at boot. Drive high to transmit. |
| 3 | RX | in | UART0 RXD | ← CH340C TXD |
| 13 | R1_GPIO | out | Relay coil (K9) | ULN2803A IN1 |
| 14 | R2_GPIO | out | Relay coil (K2) | ULN2803A IN2 |
| 16 | RX1 | in | UART2 RXD | ← MAX3485 RO |
| 17 | TX1 | out | UART2 TXD | → MAX3485 DI |
| 18 | R5_GPIO | out | Relay coil (K7) | ULN2803A IN5 |
| 19 | R6_GPIO | out | Relay coil (K5) | ULN2803A IN6 |
| 21 | SDA | — | I²C SDA | Routed, **no pull-up fitted** in rev 1 |
| 22 | SCL | — | I²C SCL | Routed, **no pull-up fitted** in rev 1 |
| 23 | R7_GPIO | out | Relay coil (K3) | ULN2803A IN7 |
| 25 | R8_GPIO | out | Relay coil (K4) | ULN2803A IN8 |
| 26 | BUZZER | out | Active buzzer | Via SS8050 NPN, 4.7 k base resistor. High = on. |
| 27 | Status_led | out | WS2812B DIN | Through 330 Ω. LED V<sub>DD</sub> is ~4.3 V (5 V via series diode). |
| 32 | R3_GPIO | out | Relay coil (K6) | ULN2803A IN3 |
| 33 | R4_GPIO | out | Relay coil (K8) | ULN2803A IN4 |
| 34 | Switch_1 | in | INPUT 1 (dry contact) | **Input-only.** 10 k pull-up to 3.3 V, 100 nF to GND, PESD3V3 clamp. Closed = low. |
| 35 | — | in | WPS / user button | **Input-only.** 10 k pull-down, button to 3.3 V. Pressed = high. |
| EN | EN | in | Reset | 10 k pull-up, 1 µF, RESET button, auto-reset transistor |

Unused and left unconnected: GPIO 4, 5, 12, 15, 36, 39 and the flash SD pins. GPIO 12 (MTDI) and GPIO 15 are unconnected so their boot-strap defaults apply.

Relay outputs are **active-high** — a high on the GPIO turns the ULN2803A channel on and energises the coil. All eight GPIOs are ordinary outputs with no boot-time side effects; the ULN inputs read low while the ESP32 is in reset, so relays stay off through boot.

## Relay ↔ terminal ↔ GPIO

Relays are numbered on the silkscreen 1–4 across the top row (left to right, viewed with the power input at the bottom right) and 5–8 across the bottom. Only the four corners carry a printed number in rev 1; the rest follow by position.

| Silk # | Terminal | Relay | ULN2803A | GPIO |
|---|---|---|---|---|
| 1 | J3 | K2 | IN2 → OUT2 | **14** |
| 2 | J9 | K5 | IN6 → OUT6 | **19** |
| 3 | J11 | K4 | IN8 → OUT8 | **25** |
| 4 | J2 | K9 | IN1 → OUT1 | **13** |
| 5 | J10 | K3 | IN7 → OUT7 | **23** |
| 6 | J6 | K8 | IN4 → OUT4 | **33** |
| 7 | J4 | K6 | IN3 → OUT3 | **32** |
| 8 | J8 | K7 | IN5 → OUT5 | **18** |

Array form, in silkscreen order:

```c
static const uint8_t RELAY_GPIO[8] = { 14, 19, 25, 13, 23, 33, 32, 18 };
```

Each terminal is **NO · COM · NC** as printed. The labelling was verified against the SANYOU SRD datasheet (Form 1c, bottom view mirrored to top): KiCad pad 1 = COM, pad 3 = NO, pad 4 = NC.

## Connectors

### J1 — power input (2-way, 5.00 mm)

| Pin | Silk | Net |
|---|---|---|
| 1 | GND | GND |
| 2 | +12V | Input, ahead of reverse-polarity FET and TVS |

12 V nominal. The downstream regulators accept 4–36 V; the input TVS stands off 18 V. Reverse connection is blocked, not clamped.

### J12 — RS-485 field bus (4-way, 5.00 mm)

| Pin | Silk | Net | Notes |
|---|---|---|---|
| 1 | 12V | +12V_Protected | Bus power for slave nodes. **No current limit in rev 1.** ~1.8 A available. |
| 2 | A | RS-485 A | Non-inverting. 330 Ω to 3.3 V fail-safe bias. |
| 3 | B | RS-485 B | Inverting. 330 Ω to GND fail-safe bias. |
| 4 | GND | GND | Bus reference |

Daisy-chain A/B/GND through each node; terminate with 120 Ω only at the two physical ends. This board's terminator is enabled by fitting a jumper on **JP1**. Bias at this node only.

### J7 — INPUT 1 (2-way, 5.00 mm)

| Pin | Net | Notes |
|---|---|---|
| 1 | Switch_1 | → GPIO34 via 10 k pull-up |
| 2 | GND | |

Dry contact (switch, reed, relay) between the two pins. No series resistance ahead of the GPIO — keep it to short, in-enclosure runs, or add ~1 k in series for field wiring.

### J5 — USB-C

USB 2.0 device. Powers the 3.3 V logic rail (not the relays) when 12 V is absent. Programming via the CH340C at up to the usual ESP32 rates; auto-reset via DTR/RTS.

### JP1 — RS-485 termination

2-pin 2.54 mm header. Jumper fitted = 120 Ω across A/B.

## Buttons

| Ref | Silk | Function |
|---|---|---|
| SW1 | RESET | Pulls EN low |
| SW2 | BOOT | Pulls GPIO0 low (hold during reset for download mode) |
| SW3 | WPS | User button on GPIO35, active-high |

## Test points

| Ref | Net |
|---|---|
| TP1 | +12V_Protected |
| TP2 | +5 V (relay rail) |
| TP3 | +3.3 V |
| TP4 | GND (wire loop) |
| TP10 | USB VBUS |

## LEDs

| Ref | Rail | Colour |
|---|---|---|
| D7 | 3.3 V | red |
| D9 | 5 V | red |
| D11 | 12 V | red |
| D15 | WS2812B status LED | RGB, GPIO27 |
