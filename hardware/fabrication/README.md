# Fabrication

Files in this directory are generated from `hardware/kicad/esp32-relay-controller.kicad_pcb` (rev 1). Regenerate them with the commands at the bottom if you change the board.

## What's here

| File | Purpose |
|---|---|
| `esp32-relay-controller-gerbers.zip` | Bare-board package: 7 gerber layers + 2 Excellon drill files. Upload this as-is. |
| `gerbers/` | The same files unzipped |
| `BOM.csv` | Master BOM — all 97 fitted parts (72 SMD + 25 THT), with description, MPN, manufacturer, footprint, type, LCSC number |
| `BOM-JLCPCB.csv` | SMD only, JLCPCB's expected columns (`Comment, Designator, Footprint, LCSC Part #`) |
| `BOM-PCBWay.csv` | SMD only, MPN-first (PCBWay quotes off manufacturer part number) |
| `CPL.csv` | SMD pick-and-place, 72 rows, `Designator, Mid X, Mid Y, Layer, Rotation`, millimetres. Works for both fabs. |

The three BOMs and the CPL were cross-validated against the board: every SMD reference appears in both the BOM and the CPL exactly once.

## Board parameters

| | |
|---|---|
| Size | 124.75 × 68 mm, rounded corners |
| Layers | 2 |
| Thickness | 1.6 mm |
| Copper | 1 oz (35 µm) |
| Min track / space | 0.20 / 0.15 mm |
| Min drill | 0.30 mm |
| Vias | 0.5 / 0.3 mm (annular 0.1 mm) |
| Surface finish | HASL is fine; ENIG if you want nicer pads under the ESP32 |
| Solder mask | Any colour. Black is what the renders show. |

Everything is inside standard 2-layer capability at JLCPCB and PCBWay.

## Ordering

### Bare PCB

Upload `esp32-relay-controller-gerbers.zip`. Nothing special to select.

### SMD assembly

**JLCPCB:** `BOM-JLCPCB.csv` + `CPL.csv`. All 35 lines carry LCSC part numbers that exist in JLC's assembly catalogue as of Sep 2026. Top side only.

**PCBWay:** `BOM-PCBWay.csv` + `CPL.csv`. LCSC numbers are included as a reference column but PCBWay sources by MPN.

Expect a DFM query on the USB-C connector: the GCT USB4105 footprint puts its two NPTH pegs 0.194 mm from the adjacent ground pads, which is under the 0.25 mm some fabs list. It's the stock footprint's own geometry and is built routinely — accept as-is.

After upload, **check the rotation preview.** KiCad and the fabs disagree on the zero-rotation reference for some packages (SOT-23, SOIC, electrolytics are the usual ones). The CPL carries correct KiCad values; the fab's preview will show anything that needs a 90° or 180° nudge, and that's corrected in their web UI, not in the file.

### Gerber / CPL origin

Both were exported against the **page origin** (not the drill/place origin). If you re-export one, re-export both the same way or the placement won't line up.

## Hand-fitted parts

The 25 through-hole parts are excluded from the assembly BOMs and CPL. All are in `BOM.csv` with LCSC numbers:

| Qty | Part | Refs | LCSC |
|---|---|---|---|
| 8 | SRD-S-105D relay, SPDT 5 V | K2–K9 | C276432 |
| 8 | Screw terminal 3-way, 5.00 mm | J2, J3, J4, J6, J8, J9, J10, J11 | C5188435 |
| 2 | Screw terminal 2-way, 5.00 mm | J1, J7 | C5188434 |
| 1 | Screw terminal 4-way, 5.00 mm | J12 | — (see note) |
| 1 | IRF4905 P-MOSFET, TO-220 | Q1 | C2564 |
| 1 | TMB12A05 active buzzer, 5 V | Buzzer1 | C96093 |
| 3 | 6 mm tact switch | SW1, SW2, SW3 | C111374 |
| 1 | 1×2 pin header, 2.54 mm | JP1 | C492401 |

**J12:** the 4-way MX126-5.0-04P was not stocked at LCSC at the time of writing. Two of the 2-way blocks (C5188434) butt together at the correct 5 mm pitch, or source any 4-way 5.00 mm block locally.

Q1 mounts vertically with no heatsink; at the board's current it dissipates single-digit milliwatts.

## Safe substitutions

- Any 0603 resistor of the same value and 1 % tolerance
- Any 0603/0805 MLCC of the same value, ≥ the stated voltage, X5R or X7R
- `CUS10S30` → any 30 V / 1 A Schottky in SOD-323
- `1N4148WT-7` → any 1N4148 in SOD-523 (D1 carries ≤ 60 mA)
- `SS8050` → any SOT-23 NPN with h<sub>FE</sub> ≥ 100 at 30 mA
- `SRD-S-105D` → SRD-05VDC-SL-C or any SRD-series 5 V Form C relay (same footprint)
- `WS2812B-B/T` → any 5050 4-pin WS2812B variant; **check pin 1 against the datasheet**, some revisions differ

Do not substitute without checking:

- `SMBJ18CA` — must be the bidirectional (`CA`) part; a unidirectional one ahead of the reverse-polarity FET shorts a reversed supply
- `BZX84-C16` — must be 16 V; it clamps the FET gate below its ±20 V limit
- `SM712` — the asymmetric ±7/12 V RS-485 array specifically; a symmetric TVS clips the bus's common-mode range
- `MAX3485` — must be a 3.3 V transceiver (the 5 V MAX485 will not work on this rail)
- `FXL0630-100-M` — anything replacing L3 needs ≥ 3 A continuous rating; it carries the field-bus current

## Regenerating these files

From `hardware/kicad/`:

```sh
kicad-cli pcb export gerbers --layers F.Cu,B.Cu,F.Mask,B.Mask,F.Silkscreen,B.Silkscreen,Edge.Cuts \
    --no-protel-ext --subtract-soldermask -o ../fabrication/gerbers/ esp32-relay-controller.kicad_pcb

kicad-cli pcb export drill --format excellon --excellon-separate-th --drill-origin absolute \
    -o ../fabrication/gerbers/ esp32-relay-controller.kicad_pcb

kicad-cli pcb export pos --format csv --units mm --side both --smd-only --exclude-dnp \
    -o ../fabrication/pos.csv esp32-relay-controller.kicad_pcb
# then rename columns to Designator / Mid X / Mid Y / Layer / Rotation and normalise rotation to 0–360
```

**Refill zones before plotting** (`B` in the PCB editor, or `--check-zones` on the CLI export). The CLI does not refill by default, and a stale fill plots exactly as saved.
