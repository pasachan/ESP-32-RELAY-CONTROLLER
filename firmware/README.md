# Firmware

No firmware is included in this repository yet. This file carries the hardware facts a firmware author needs on day one; the full table is in [`docs/pinout.md`](../docs/pinout.md).

## Pin definitions

```c
// Relays, in silkscreen order 1–8. Active-high into a ULN2803A.
static const uint8_t RELAY_GPIO[8] = { 14, 19, 25, 13, 23, 33, 32, 18 };

// RS-485 (MAX3485, half duplex) on UART2
#define RS485_TX_GPIO     17
#define RS485_RX_GPIO     16
#define RS485_DIR_GPIO     2   // high = transmit. External 10k pull-down → receive at boot.

// Inputs
#define INPUT1_GPIO       34   // dry contact to GND, external 10k pull-up, closed = LOW. Input-only pin.
#define USER_BUTTON_GPIO  35   // pressed = HIGH, external 10k pull-down. Input-only pin.

// Indicators
#define BUZZER_GPIO       26   // high = on
#define STATUS_LED_GPIO   27   // WS2812B data, single pixel

// I2C — routed to GPIO21/22 but no pull-ups fitted in rev 1
#define I2C_SDA_GPIO      21
#define I2C_SCL_GPIO      22
```

## Things worth knowing

- **Relays are off through reset and boot.** The ULN2803A inputs read low while the ESP32 pins are high-impedance. Initialise the relay GPIOs low before setting them as outputs to avoid a glitch.
- **GPIO2 (RS-485 direction) is a boot-strapping pin.** The external pull-down keeps it low at boot, which is what the ESP32 needs and also parks the transceiver in receive mode. Don't add an internal pull-up.
- **GPIO34 and GPIO35 are input-only** and have no internal pull resistors; the board provides external ones.
- **Turn-around on the RS-485 bus:** raise `RS485_DIR_GPIO` before writing, wait for the UART TX FIFO to drain (`uart_wait_tx_done`), then drop it. Dropping it early truncates the last byte.
- **The 5 V rail is absent on USB-only power**, so relays, the buzzer and the WS2812B do nothing while you're programming from a laptop. The 3.3 V logic rail works normally.
- **Module is the 16 MB flash variant** (ESP32-WROOM-32D-N16). Pick a matching partition table.
