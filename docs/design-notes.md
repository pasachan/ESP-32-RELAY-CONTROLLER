# Design notes

Decisions, the reasoning behind them, and what a pre-fabrication review changed. Numbers quoted are measured from the KiCad files or taken from the referenced datasheets, not estimated.

Contents

- [Power input and protection](#power-input-and-protection)
- [Regulators](#regulators)
- [Relay bank and switched-load clearances](#relay-bank-and-switched-load-clearances)
- [Relay coil drive margin](#relay-coil-drive-margin)
- [RS-485 field bus](#rs-485-field-bus)
- [Why the bus carries 12 V, not 3.3 V](#why-the-bus-carries-12-v-not-33-v)
- [USB](#usb)
- [Status LED level shifting](#status-led-level-shifting)
- [Indicator LEDs](#indicator-leds)
- [Layout notes](#layout-notes)
- [Design-rule gotcha: net names with parentheses](#design-rule-gotcha-net-names-with-parentheses)
- [What the pre-order review caught](#what-the-pre-order-review-caught)
- [Rev 2 backlog](#rev-2-backlog)

---

## Power input and protection

```
J1 (+12 V) ──► D2 SMBJ18CA ──► Q1 IRF4905 ──► L3 10 µH ──► +12V_Protected ──► bucks, field bus
                (to GND)        P-FET             ▲
                                                  C1 470 µF · C2 100 µF · C12 10 µF
```

**Reverse-polarity protection is a P-channel MOSFET, not a diode.** Q1 (IRF4905, 20 mΩ) is wired drain-to-input, source-to-load, gate pulled to ground through 100 k with a 16 V zener (D5, BZX84-C16) clamping V<sub>GS</sub>. At the board's ~600 mA draw that is 12 mV of drop and 7 mW of loss; a Schottky in the same position would drop ~400 mV and burn 240 mW. With the input reversed the body diode is reverse-biased and V<sub>GS</sub> is zero, so nothing conducts.

The gate zener's orientation was worth checking against the part rather than the symbol: the BZX84 in SOT-23 is pin 1 = anode, pin 2 = no connection, pin 3 = cathode. The schematic uses KiCad's `Diode:BZX84Cxx` symbol, which has that pinout; a generic two-pin zener symbol on a three-pad footprint would put the anode on the NC pin and silently remove the clamp on the next schematic-to-PCB sync.

**The TVS sits ahead of the FET, and is bidirectional.** An earlier revision had a unidirectional SMAJ18A *after* the input inductor. A surge on the input then reached Q1's drain (rated −55 V) before anything clamped it, and L3's V = L·di/dt during the edge added tens of volts on top of the clamp voltage. Moving the clamp to the connector fixes that — but a *unidirectional* TVS ahead of the reverse-polarity FET would forward-conduct on a reversed supply and short it, defeating Q1. Hence SMBJ18**CA**: 18 V standoff both ways, 600 W.

**L3 is an EMI filter, not surge protection.** Two synchronous bucks switching at ~1.1 MHz draw pulsed current from the +12 V node. L3's impedance at that frequency is ~69 Ω against C12's ~0.05 Ω, giving roughly 63 dB of attenuation before the ripple reaches the supply wiring. The original 22 µH part in a 4.4 × 4.2 mm package was rated ~1.5 A and became the limiting element once the board started supplying a field bus; it was replaced with a 10 µH / 4.5 A part (FXL0630-100-M) on the same footprint already used for L1 and L2. The 7 dB of filtering given up is immaterial; the LC resonance moves from ~1.4 kHz to ~2.1 kHz, which is if anything better for stability against the bucks' constant-power input characteristic.

## Regulators

Two LMR51430 (4–36 V in, 3 A, SOT-23-6):

| | V<sub>out</sub> | Divider | Feeds |
|---|---|---|---|
| U2 | 0.6 × (1 + 100k / 13.7k) = **4.98 V** | R1 / R2 | Relay coils, buzzer, WS2812B |
| U3 | 0.6 × (1 + 100k / 22.1k) = **3.32 V** | R3 / R4 | ESP32, CH340C, MAX3485 |

EN is tied to VIN on both, which the LMR51430 datasheet gives as the recommended self-start configuration.

U3's input is diode-ORed from +12 V (D3) and USB VBUS (D4), so the logic rail runs from USB alone for programming on the bench. The 5 V rail does not — relays are deliberately inert on USB power. USB minus one Schottky drop is ~4.65 V, above the LMR51430's 4 V minimum, and the ~71 % duty cycle is well inside the part's range.

Switch-node copper is 5–6 mm² per buck, single-layer, no vias; the feedback traces have zero parallel run within 1.5 mm of either switch node. Input ceramics are 0.7 mm and 1.0 mm from the VIN pins.

## Relay bank and switched-load clearances

Eight SRD-05VDC-SL-C relays (Form C, 10 A / 250 VAC contacts) driven by a ULN2803A. COM pin 10 of the ULN is tied to the 5 V coil rail so the internal clamp diodes provide the flyback path.

**This revision is laid out for low-voltage loads.** The contact traces are 1.0 mm on 1 oz copper — about 2.3 A at 10 °C rise, 3.1 A at 20 °C. On the bottom layer the eight coil-drive traces run through the relay contact-pin field at **0.2 mm** from the contact pads, and the ground pour comes to **0.5 mm** of them. Both are fine for 12–30 V (IPC-2221 external needs 0.1 mm) and would be unacceptable for mains: 230 V needs ≥ 1.25 mm even functionally, and a safety barrier under IEC 62368 wants 3 mm+ of creepage with a routed slot between the switched side and the controller. Adapting this board for mains is a relay-bank re-layout, not a rule change.

The silkscreen NO / NC labelling was verified against the SANYOU SRD datasheet rather than the generic KiCad symbol. The datasheet's Form 1c wiring diagram (bottom view) places the resting contact on what becomes KiCad pad 4 after mirroring to top view — so pad 4 is NC and pad 3 is NO. All 24 labels match.

## Relay coil drive margin

SRD-05VDC: 70 Ω coil, 71.4 mA, must-operate at 75 % of nominal = **3.75 V**.

```
5 V rail (nominal)                4.98 V
ULN2803A V_CE(sat) @ 71 mA       −0.90 V  (typ; 1.1 V max)
                                 ────────
Coil voltage                      4.08 V   (3.88 V at max V_CE(sat))
```

An earlier revision fed one relay through a series Schottky to allow it to operate on USB power. That channel's coil then sat at 3.73 V — under the pick-up spec at typical values and near 3.3 V worst case. The diode was removed; all eight channels now share the same 0.3 V of margin. If more headroom is ever wanted, R2 = 13.0 k lifts the rail to 5.22 V.

Total ULN2803A dissipation with all eight coils energised is ~0.5 W in SOIC-18W — within its rating, warm to the touch.

## RS-485 field bus

MAX3485 (3.3 V, half-duplex), with RE̅ and DE tied together on GPIO2.

**GPIO2 is a strapping pin, and that is used deliberately.** R22 (10 k) pulls it to ground, so during reset and boot the transceiver is in receive-only mode and cannot drive the bus while the MCU is undefined. The pull-down also satisfies the ESP32's boot-strapping requirement for that pin.

**Termination** is 120 Ω (R23) in series with a jumper (JP1) across A/B — fitted only at the two physical ends of the bus.

**Fail-safe bias** is 330 Ω from 3.3 V to A (R26) and 330 Ω from B to ground (R27). With one 120 Ω terminator that holds the idle differential at ~508 mV; with terminators at both ends, ~275 mV — both above the 200 mV receiver threshold. The 330 Ω pair is on the heavy side: with two terminators the DC load works out to ~55 Ω against RS-485's 54 Ω minimum, so **only this node should bias**. If another node also needs to, 560 Ω–1 k here would give headroom.

**Protection** is an SM712 (D6), the asymmetric 7 V / 12 V array made for RS-485's −7 to +12 V common-mode range. The datasheet specifies it as "Pin 3-1 and Pin 3-2", confirming pin 3 as common; KiCad's `Diode:SM712_SOT23` symbol matches.

The connector is 12 V · A · B · GND. Silkscreen matches the netlist.

## Why the bus carries 12 V, not 3.3 V

The first idea was to put 3.3 V on the field connector so slave boards need no regulator. Three things kill that.

**Cable drop.** 24 AWG is 0.168 Ω/m round-trip. Eight nodes at 30 mA each over a 30 m run:

```
distributing 3.3 V:  240 mA × 30 m × 0.168 = 1.2 V drop  →  node sees 2.1 V   (dead)
distributing 12 V:   240 mA × 30 m × 0.168 = 1.2 V drop  →  node sees 10.8 V  (fine)
```

Same absolute drop; on 3.3 V it kills the remote transceiver, on 12 V the local regulator never notices.

**The 3.3 V buck's thermal ceiling.** U3 is a SOT-23-6 at ~82 % efficiency from 12 V. One amp out is ~0.7 W of loss — roughly 90 °C of rise — so it can realistically supply the board's own ~600 mA peak plus perhaps 200–300 mA more. Not a bus.

**Fault isolation.** A short on a 3.3 V field pin collapses the same rail the ESP32 runs on. On the 12 V rail there is 570 µF of bulk and the input protection between the fault and the controller.

So each slave regulates locally. At 30 mA a 12 → 3.3 V LDO burns 0.26 W (use SOT-89 or a small buck); the feed pin has ~1.8 A available, and RS-485's 32-unit-load limit will be reached long before the current budget is.

Rev 1 has no current limiting on the 12 V feed pin — see the [rev 2 backlog](#rev-2-backlog).

## USB

USB 2.0 Full Speed (12 Mbps). At that speed a 4 ns edge is ~60 cm long in FR-4 and impedance control is irrelevant for anything under ~10 cm. D+ is 24.6 mm, D− 22.7 mm (two vias), skew 1.8 mm ≈ 12 ps. The pair is 35 mm from the nearest switch node and 24 mm from the nearest relay coil trace. The only tracks crossing under it on the back layer are the UART0 lines.

5.1 k pull-downs on CC1 and CC2 (correct sink configuration), USBLC6-2SC6 ESD array on D+/D−/VBUS, 4.7 µF on VBUS. The CH340C runs at 3.3 V with V3 tied to VCC; the standard two-transistor DTR/RTS auto-reset circuit drives EN and GPIO0.

## Status LED level shifting

A WS2812B needs V<sub>DD</sub> between 3.5 and 5.3 V and defines V<sub>IH</sub> as 0.7 × V<sub>DD</sub>. From a 5 V supply that is 3.5 V — above what a 3.3 V GPIO can drive — and running the LED from 3.3 V is below its minimum supply. The window that satisfies both is 3.5–4.71 V.

The fix is a 1N4148 in series with the LED's supply (D1) with the 100 nF bypass (C17) on the LED side of it:

```
                  V_f      V_DD at LED    V_IH = 0.7·V_DD    3.3 V drive
low brightness   0.65 V      4.35 V          3.05 V             ✓
full white       0.90 V      4.10 V          2.87 V             ✓
```

One diode instead of a level shifter, with 250–400 mV of margin.

## Indicator LEDs

Three 0201 red LEDs (V<sub>f</sub> ≈ 1.9 V) on the 3.3 V, 5 V and 12 V rails. The 3.3 V one is the constraint: an early pick was a white 0201 at V<sub>f</sub> 2.6–3.2 V, which on a 3.3 V rail through 1 k gives 0.4 mA at typical V<sub>f</sub> and 0.1 mA at the top of the spread — invisible, and dominated by part-to-part variation rather than the resistor. Red through 330 Ω gives 4.2 mA and doesn't care about V<sub>f</sub> spread.

## Layout notes

- 2-layer, solid ground pour on the bottom, 30 stitching vias plus 24 through-hole ground pads.
- ESP32 antenna section overhangs the board edge by 6.5 mm; all castellated pads are on copper and the module's keep-out is entirely off-board.
- The last ~9 mm of 3.3 V into the ESP32 is 0.2 mm trace (22 mΩ, 11 mV at a 500 mA Wi-Fi burst); C21 (10 µF) sits 3 mm from the pin and C14 (100 nF) 1 mm.
- The USB-C receptacle sits 0.58 mm behind the board edge — inside KiCad's edge marker for that footprint. Some cables seat fully, some don't; a light file on the edge fixes it on a built board.
- Netclass clearance is 0.15 mm; hole-to-copper is 0.18 mm to accommodate the GCT USB-C footprint's own 0.194 mm NPTH spacing. Both inside standard 2-layer capability at JLCPCB and PCBWay.

## Design-rule gotcha: net names with parentheses

The project's custom DRC rules for the relay bank originally read:

```
(rule "Relay contact clearance 1mm"
  (condition "A.NetName == 'Net-(J10-Pin_1)' || A.NetName == 'Net-(J2-Pin_2)' || ...")
  (constraint clearance (min 1.0mm)))
```

They never fired. A canary rule on `'GND'` produced 507 violations, a rule on `'Relay1'` produced 28, but any condition matching a name of the form `Net-(Jx-Pin_y)` produced zero — KiCad 10's rule parser does not match net names containing parentheses, and it does not warn. The board passed DRC with 0.2 mm clearances against a rule demanding 1.0 mm.

The fix in this repository: the project file assigns those nets to a `RelayContacts` netclass by the pattern `Net-(J*-Pin_*)`, and the rules match on `A.NetClass == 'RelayContacts'`. Verified by a 5 mm probe rule against the class (244 hits) before setting the real limits.

The limits now in `esp32-relay-controller.kicad_dru` are what a low-voltage board should meet, and this board does: ≥ 1.0 mm between different switched-load nets, ≥ 0.5 mm to the ground pour, ≥ 0.2 mm to everything else.

## What the pre-order review caught

Before ordering, the design went through KiCad DRC and ERC, a schematic-parity check, and an independent geometric connectivity and clearance pass over every pad, track, via and zone polygon. Things it found and that were fixed, roughly in order of severity:

1. **Eight broken nets in an intermediate layout** — the 5 V buck's ground pin, the CH340C's 3.3 V feed and the I²C pull-ups were each on isolated copper islands. KiCad's DRC reported them as unconnected items; the geometric pass found the same eight independently.
2. **A stale zone fill** that overlapped USB D− with the ground pour. `kicad-cli pcb export gerbers` does not refill zones by default; plotted as-is that board would have shipped with USB shorted.
3. **A 2-pin zener symbol on a 3-pad SOT-23 footprint.** The copper was right; the schematic was wrong, and a schematic-to-PCB sync would have moved the anode net onto the package's NC pin.
4. **Two input terminals swapped between schematic and layout**, and a series Schottky that put one relay coil below its pick-up voltage.
5. **A 0.035 mm clearance** between +12 V and ground after a rework of the input filter.
6. **An inductor whose BOM part didn't fit its footprint** — pad pitch differed by 0.7 mm between the part ordered and the land pattern on the board.
7. **Part-number fields that lagged value changes** — after a resistor was changed from 1 k to 330 R and an inductor from 22 µH to 10 µH, the LCSC / MPN fields still pointed at the old parts. Worth a final field-consistency check on any BOM exported from a schematic that has been edited.
8. **A field bus connector with 3.3 V on it** — see [above](#why-the-bus-carries-12-v-not-33-v).

Two things it checked and left alone, which is also worth recording: the NO / NC silkscreen (a first pass using the generic relay symbol's drawn contact suggested all 16 were swapped; the SRD datasheet showed the silkscreen was right), and the SHT4x pinout (pin 2 really is SCL — the datasheet confirms it, against a common expectation that it is ground).

## Rev 2 backlog

Nothing here affects rev 1 functioning as designed for low-voltage loads.

- **Current-limit the 12 V field-bus pin.** A 500 mA–750 mA PPTC in series with J12 pin 1 prevents a shorted cable becoming a sustained fault; a dedicated small buck for the bus would additionally keep the controller from browning out during one. Add an 18 V TVS at the connector while there — the input TVS is upstream of the filter and does not protect this node.
- **Reorder the field connector to A · B · GND · 12 V** so ground separates the bus pair from the power pin; a one-position wiring slip currently puts 12 V on the RS-485 line and through the SM712.
- **Move the coil-drive traces out of the relay contact field**, widen contact traces to 2.5–3 mm or go to 2 oz, and add a slot under the relay bank — the pre-requisites for any mains variant.
- **Add I²C pull-ups (or a header)** — GPIO21/22 are routed but unpopulated after the on-board humidity sensor was dropped for space.
- **Widen 3.3 V distribution to 0.5 mm**, route D+/D− together on one layer, and put the USB ESD array in-line rather than on a detour.
- **Silkscreen numbers on all eight relays** (only the four corners are labelled), and clear the remaining silk-over-pad overlaps.
- **Nudge the USB-C receptacle 0.58 mm outward** so its mating face is flush with the board edge.
- A few more ground stitching vias along the buck area and around the ESP32.
