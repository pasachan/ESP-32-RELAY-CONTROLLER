# Firmware

[`self-test/`](self-test/) is a hardware bring-up sketch that exercises every fitted component and prints a pass/fail report over USB serial. It needs no libraries beyond the Arduino-ESP32 core. Run it first on a new board — it validates the relay silkscreen-to-GPIO mapping, which is not in an obvious order.

No application firmware is included yet. The rest of this file carries the hardware facts a firmware author needs on day one; the full table is in [`docs/pinout.md`](../docs/pinout.md).

## Pin definitions

```c
// Relays, in silkscreen order 1–8. Active-high into a ULN2803A.
static const uint8_t RELAY_GPIO[8] = { 14, 19, 25, 13, 23, 33, 32, 18 };

// RS-485 (MAX3485, half duplex) on UART2
#define RS485_TX_GPIO     17
#define RS485_RX_GPIO     16
#define RS485_DIR_GPIO     2   // high = transmit. External 10k pull-down → receive at boot.
                               // After Serial2.begin(), pull RS485_RX_GPIO up — see below.

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
- **Turn-around on the RS-485 bus:** raise `RS485_DIR_GPIO` before writing, wait for the UART TX FIFO to drain (`uart_wait_tx_done`, or `Serial2.flush()` under Arduino), then drop it. Dropping it early truncates the last byte.
- **Pull the RS-485 RX pin up in software.** DE and RE̅ are tied together, so enabling the driver also disables the receiver — and a disabled MAX3485 drives RO to high impedance. Rev 1 fits no pull-up on RO, so GPIO16 floats for the whole of every transmission and the UART clocks in phantom bytes off the adjacent DI line. Measured on the first board: ~5 junk bytes per 18-byte frame at 9600 baud, with nothing attached to the bus. One line fixes it:

  ```c
  Serial2.begin(9600, SERIAL_8N1, RS485_RX_GPIO, RS485_TX_GPIO);
  gpio_set_pull_mode((gpio_num_t)RS485_RX_GPIO, GPIO_PULLUP_ONLY);  // must follow begin()
  ```

  That took the count to zero. A 10 kΩ pull-up on RO is in the rev 2 backlog.
- **The 5 V rail is absent on USB-only power**, so relays, the buzzer and the WS2812B do nothing while you're programming from a laptop. The 3.3 V logic rail works normally.
- **Module is the 16 MB flash variant** (ESP32-WROOM-32D-N16). Pick a matching partition table — `default_16MB.csv` under PlatformIO, or "16M Flash (3MB APP/9.9MB FATFS)" in the Arduino IDE.
- **Flash at 460800, not 921600.** The faster rate fails partway through the handshake with `Invalid head of packet`; the CH340C and the USB-C stub do not carry it reliably.
