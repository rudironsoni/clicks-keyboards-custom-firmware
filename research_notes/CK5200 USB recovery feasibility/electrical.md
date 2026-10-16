# CK-5200 electrical architecture and conditional USB-C recovery adapter

Research date: 2026-10-16. Scope: existing photos, repository evidence, manufacturer documents, and offline calculations. This is a conceptual feasibility study, not a construction or keyboard-test procedure. Boot-entry semantics, boot code integrity, and ISP command safety remain dependencies on the separate boot researcher.

Evidence labels used throughout:

- **PROVEN (scope stated):** the cited photo, document, or calculation directly establishes the stated fact. A datasheet fact does not establish CK-5200 wiring.
- **SUPPORTED HYPOTHESIS [INFERENCE]:** evidence supports the explanation, but does not prove it on this keyboard.
- **BLOCKED [UNVERIFIED]:** a required fact is missing. No working electrical path or safe operating window is claimed.
- **DISPROVEN (scope stated):** cited evidence contradicts the specific claim. This does not disprove every possible adapter.

## 1. What do the existing photos establish about the MCU and board?

### Takeaway

**PROVEN (photographed package):** U3 has 48 leads, 12 per side. **SUPPORTED HYPOTHESIS [INFERENCE]:** the firmware-family evidence plus that package supports CH32X035C8T6, but the marking does not establish the exact part. Connector-to-pin continuity remains **BLOCKED**. [Photos][photos]; [prior photo report][photo-report]; [family evidence][port]

### Cited Findings

All 24 JPEG views were inspected in six contact sheets, with detailed crops used for markings and pad checks. All 64 entries in the existing photo manifest passed SHA-256 verification. HEIC originals were integrity-checked; this pass used the JPEG views and existing derivatives for visual interpretation. The image-to-original mapping comes from the existing report, not a new independent image-matching run. [Manifest][manifest]; [mapping, report §2][photo-report]

The following log records visible features, not electrical connections. Each numbered link points to the source JPEG.

| View | Relevant observation | Limit |
| --- | --- | --- |
| [01][p01] | Close U3 view, four lead rows, faint lower marking | Exact manufacturer and part unreadable |
| [02][p02] | Whole visible PCB, two flex connector areas, lower metal connector, U3 | No complete USB or supply route visible |
| [03][p03] | Lifted flex with `IP15-KB-TYPEC` text | Flex conductor assignments unknown |
| [04][p04] | Left-side power/component area, inductor marked `120`, Q6/Q7 area | Inductor value and circuit function not established |
| [05][p05] | Six rectangular gold pads, `VBUS_P`, `UD_*`, `DP` label area | Pad functions and disputed text unresolved |
| [06][p06] | U6 beside lower connector and passive components | U6 identity and signal function unknown |
| [07][p07] | USB label block, six pads, four labeled pads | Labels do not prove continuity; glare affects small text |
| [08][p08] | Flex installed, `MAIN` and `TYPEC` visible | Shows assembly relationship, not net assignments |
| [09][p09] | U3 leads clear enough to support 12 per side | Pin-1 orientation not established |
| [10][p10] | Q6/Q7 eight-lead packages and nearby three-lead parts | Top markings insufficient for exact identities |
| [11][p11] | Whole board with flex removed, six pads and U3 visible | No exposed complete route to inner plug |
| [12][p12] | U3 package and faint marking from another angle | Supports package count, not exact part |
| [13][p13] | Installed flex `MAIN`/`TYPEC`, whole board | No visible battery or BOOT/reset button |
| [14][p14] | Q5 marking resembles `A19T`; Q9 resembles `.7002` | Marking observations only, not verified transistor types |
| [15][p15] | Alternate Q6/Q7 close view | No reliable full part number |
| [16][p16] | Oblique U3 and power-area view | Traces disappear under components and glare |
| [17][p17] | Left-side U1/inductor/Q6/Q7/U6 area | Supports a populated power circuit, not its topology |
| [18][p18] | Installed flex and U6/lower connector area | Data switch or hub role not established |
| [19][p19] | Whole board; U3 lower marking and U7 area visible | Higher resolution crops needed for marking limits |
| [20][p20] | Flex sockets, Q6/Q7, several visible trace segments | No uninterrupted inner-plug D+ route |
| [21][p21] | `3V3 / SWD / GND / SWC` pads, U3 edge, USB text | Pad-to-lead mapping still unknown |
| [22][p22] | U3 close view and four pads | Strong package evidence; top marking unreadable |
| [23][p23] | U7 three-lead-plus-tab package near lower connector | Package suggests a power role, does not prove it |
| [24][p24] | U3 and pads, clear lower marking | Best read remains `15PM`, with worn first characters |

Detailed crop findings:

- **PROVEN (image scope):** U3 is a 48-lead QFP. **DISPROVEN (package claim):** the earlier 64-lead count is inconsistent with the visible 12 leads per side. `15PM` is a tentative marking transcription, not a WCH part number. Multiple circular impressions are present; this pass does not assign pin 1 from them. [U3 p19 crop][u3]; [p22 crop][u3-22]; [p24 crop][u3-24]; [pin-count crops][crops]
- **PROVEN (silkscreen scope):** four round pads are labeled `3V3 / SWD / GND / SWC`. Traces near SWD/SWC disappear before an identifiable MCU lead. Six rectangular pads near the flex connector remain unlabeled and unassigned. There is no readable BOOT or reset pad, and no visible recovery button in these views. Absence in these views is not proof of absence on the hidden PCB side. [Pad crops][crops]; [views 07][p07], [21][p21]; [report E3-E7/E12][photo-report]
- **SUPPORTED HYPOTHESIS [INFERENCE]:** U7's crop resembles a logo, `B239A`, and `2410/50`. Earlier reads differ, including `?239A` and `2410-9?0`. U1 and U5 are unreadable. No exact regulator, switch, charger, or ESD part is identified from these markings. [U7 crop][u7]; [U1 crop][u1]; [U5 crop][u5]; [report E9][photo-report]
- **[CORRECTION] PROVEN (crop contents):** `crops/q6-marking.jpg` shows a blurry small component beside U3, not the eight-lead Q6 shown in photos 10/14/15. `crops/u3-marking-p21.jpg` actually centers the smaller U5 area and does not show the U3 marking. Neither filename proves component identity. [Q6-named crop][q6-crop]; [U3-p21-named crop][u3-21]; [photo 14][p14]
- **PROVEN (manufacturer statement):** Clicks says its keyboard case contains no battery and draws power from the phone. This resolves the earlier report's battery uncertainty at product-document level. It does not establish rail discharge time or exclude another attached supply. [Clicks, "Does Clicks have a battery?"][clicks]; [earlier report §7][photo-report]

Conditional package map, verified against WCH V2.3 PDF p.13, printed p.11, and Table 2-1, PDF pp.17-19. The C8T6 evaluation schematic also agrees. These are **PROVEN (C8T6 documentation)**, not identified physical leads on the photographed board. [WCH datasheet][ds]; [WCH schematic, PDF p.4][sch]

| Function | C8T6 LQFP48 pin | CK-5200 connection status |
| --- | ---: | --- |
| PC16 / UDM / D−, bonded PC11 | 32 | Inner plug D− route unknown |
| PC17 / UDP / D+, bonded PC10 | 33 | Inner plug D+ route unknown |
| PC18 / DIO | 34 | SWD-labeled pad association unverified |
| PC19 / DCK | 37 | SWC-labeled pad association unverified |
| PC14 / CC1 | 38 | Use as CC on this board unknown |
| PC15 / CC2 | 39 | Use as CC on this board unknown |
| GND | 47 | Ground-pad continuity unverified |
| VDD | 48 | `3V3` pad continuity and actual voltage unverified |
| PA21 / RST alternate function | 7 | No identified external reset path |

WCH notes that PC10/PC17 and PC11/PC16 are internally bonded for this variant. Both members of a pair must not be configured as outputs together; PC10/PC11 should be floating inputs in USB use. External assertion of D+ therefore reaches the bonded PC17/PC10 node if the connector route exists. [DS, PDF p.20, note 4][ds]

### Inferences

- **SUPPORTED HYPOTHESIS [INFERENCE]:** a phone-facing USB data path to the keyboard controller must exist during normal operation, but it could contain a switch, bridge, hub, protection device, or firmware-controlled gate. Normal typing does not prove direct DC access to PC17 during reset. [Clicks direct-connection FAQ][clicks]; [photos][photos]; [firmware-family evidence][port]
- **SUPPORTED HYPOTHESIS [INFERENCE]:** the left-side inductor, ICs, and transistor packages indicate power conversion or switching. They do not establish which side supplies U3, whether VDD is 3.3 V, or whether the plug's VBUS is directly coupled to VDD. [04][p04]; [17][p17]; [20][p20]

### Gaps

**BLOCKED:** exact MCU marking, pin-1 orientation, inner-plug conductor mapping, all pad-to-MCU connections, identities of U1/U5/U6/U7/Q6/Q7, and all power-path switch states. Photos cannot establish copper continuity through hidden layers, flex traces, or components. No electrical measurement was made.

## 2. Can the inner male USB-C connector expose D+/PC17 before boot sampling?

### Takeaway

**BLOCKED:** no available photo or model-matched schematic proves that the inner plug's D+ reaches PC17 while power rises. The correct conceptual mating point is the phone-facing male plug, using a female host receptacle; the bottom charging port is not an established recovery data port. [Photos][photos]; [Clicks USB-C charging/data FAQ][clicks]

### Cited Findings

- **PROVEN (connector standard):** USB 2.0 D+ is on receptacle A6/B6, D− on A7/B7. A USB 2.0 receptacle joins each same-polarity pair to support either insertion orientation. CC1/A5 and CC2/B5 remain separate. [Type-C R2.4, pin assignment Table 3-1 and §4.5.1.1, p.158][typec]
- **PROVEN (manufacturer behavior statement):** Clicks for iPhone 15 Pro USB-C supports charging through the bottom port, but not simultaneous phone data passthrough. The same FAQ explicitly distinguishes iPhone 16 passthrough support. This is not a schematic-level proof that every data contact is absent or disconnected in every mode. [Clicks, "Can I use wired CarPlay and transfer data with Clicks?"][clicks]
- **PROVEN (MCU documentation):** WCH says PC17 is the BOOT detection pin and high at power-up causes entry to BOOT. This electrical study does not infer the exact sampling instant, reset equivalence, boot code integrity, or ISP availability from that sentence. [DS, PDF p.21, note 8][ds]; [existing recovery audit][audit]
- **PROVEN (reference circuit):** WCH's C8T6 evaluation schematic, PDF p.4, DOWNLOAD block, connects `VCC -> R5 4.7K -> S4 Download -> PC17`. Its VCC is also U1 MCU VDD. The board's power block selects 3V3 or 5V for VCC. This is a local-rail pull-up, not an independent adapter supply applied to an unpowered MCU. The schematic's USB protection and CC resistors describe the evaluation board only. [WCH evaluation schematic][sch]

Connectivity ledger:

| Required edge or state | Evidence class | Missing fact |
| --- | --- | --- |
| Inner plug D+ -> flex -> main-board net | BLOCKED | Full conductor/net assignment |
| Main-board D+ -> series parts -> U3 pin 33 | BLOCKED | Continuity and component values |
| Inner plug D− -> U3 pin 32 | BLOCKED | Continuity and component values |
| ESD protection on D+/D− | BLOCKED | Part, topology, capacitance, clamp destinations |
| USB mux or buffer in that path | BLOCKED | Presence, supply, default state, firmware dependency |
| Inner plug VBUS -> U3 VDD | BLOCKED | Regulator/switch path, thresholds, delay, discharge |
| Inner plug CC -> Rd at zero VDD | BLOCKED | Passive or dead-battery termination before application startup |
| Bottom port -> inner plug / local supply | BLOCKED | Reverse-blocking and charging topology |
| GND and shell paths | BLOCKED for board mapping | Standard pin assignments are known, actual routing is not |

The ledger follows the photographed trace limits and the existing report's §6/§10. No unknown edge is drawn as proven in the adapter diagram. [Photo report][photo-report]; [photos][photos]

### Inferences

- **SUPPORTED HYPOTHESIS [INFERENCE]:** if the inner plug reaches PC17 through only passive low-resistance elements, and the boot researcher establishes a compatible sampling window, an external high level could reach that node. If an intermediate switch stays open until application firmware runs, this path cannot recover an application that never enables it. [DS pin mapping][ds]; [routing limits][photo-report]
- **DISPROVEN (reasoning claim):** `DP`/`DM` silkscreen beside the lower connector is not proof that this port exposes the MCU. The text is visible, but no complete route was established; the manufacturer's iPhone 15 behavior also does not support ordinary passthrough. [07][p07]; [Clicks FAQ][clicks]

### Gaps

**BLOCKED:** no model-matched schematic, netlist, BOM, or existing continuity record establishes these edges. The documents cannot distinguish a passive path from a gated path. A recovery adapter cannot be approved from this evidence.

## 3. What are the power, CC, and D+ electrical limits?

### Takeaway

**BLOCKED:** an independent 3.3 V pull-up cannot be assumed safe before the keyboard rail rises. A resistor limits current but does not prove compliance with the MCU's pin-voltage limit, and a connector VBUS measurement does not measure internal VDD. [DS, Tables 3-1/3-2/3-13][ds]

### Cited Findings

| Parameter | Documented value | Scope and source |
| --- | --- | --- |
| MCU I/O absolute voltage | −0.3 V to VDD + 0.3 V | DS PDF p.25, Table 3-1; damage boundary, not recommended operation [DS][ds] |
| VDD with USB or PD enabled | 3.0 to 5.3 V | DS PDF p.26, Table 3-2 [DS][ds] |
| Standard GPIO high threshold | 1.8 V at VDD=3.3 V; 2.4 V at VDD=5 V | DS PDF p.29, Table 3-13; applicability to boot detection unverified [DS][ds] |
| Standard GPIO input leakage | −2 to +2 µA | Same table; not a whole-board leakage bound [DS][ds] |
| POR/PDR threshold | Typical 1.8/1.78 V | DS PDF p.26, Table 3-4; no guaranteed threshold range supplied in that row [DS][ds] |
| Power-on reset delay | 4/17/24 ms minimum/typical/maximum | Same table; not proof of PC17 sample time [DS][ds] |
| Host D+/D− pulldowns | 14.25 to 24.80 kΩ, nominal 15 kΩ | USB resistor ECN pp.1-3, §7.1.5.1 [ECN][ecn] |
| Type-C sink Rd | Nominal 5.1 kΩ | Type-C Table 4-28, p.244; alternatives and tolerances specified there [Type-C][typec] |
| Default source Rp | 56 kΩ to 4.75-5.5 V, or 36 kΩ to 3.3 V ±5% | Type-C Table 4-27, p.243; use complete table, not a bare universal resistor rule [Type-C][typec] |

**PROVEN (USB requirements):** a source receptacle keeps VBUS off until sink attach and removes it on detach. CC establishes attach and roles. D+ bias is not CC attach and does not replace Rp/Rd. USB reset actively drives the data pair to SE0; leaving the host connected during boot assertion introduces both pulldowns and potential active contention. [Type-C §§4.4.2, 4.5, 4.5.2.2.9, pp.152, 158, 187-188][typec]; [USB 2.0 §§7.1.5, 7.1.7.5][usb2]

**PROVEN (source-document discrepancy):** the resistor ECN's summary and normative replacement text specify 14.25 kΩ, but the embedded Figure 7-20/7-21 labels print 14.75 kΩ. This study uses the explicit 14.25 kΩ lower bound, not the inconsistent figure label. [ECN pp.1-4][ecn]

### Inferences

#### Conditional bias calculations, not selected keyboard values

**[INFERENCE, calculated]** Define:

```text
VB       = adapter bias voltage
R        = Rbias + switch resistance + keyboard path resistance
RD       = total effective pulldown resistance at the target node
IL       = worst-case additional current drawn from that node
VK(t)    = actual keyboard MCU VDD
VHboot   = verified BOOT high threshold, currently unknown

Vnode_min = (VB_min - IL_max * R_max) * RD_min / (RD_min + R_max)
           (passive divider model; no active driver or clamp conducting)

With RD effectively infinite:
Vnode_min = VB_min - IL_max * R_max

Required logic condition: Vnode_min >= VHboot + margin
Necessary absolute bound: -0.3 <= Vnode(t) <= VK(t) + 0.3
Required design condition: meet normal operating limits, with margin,
                          not merely the absolute bound.
```

These equations use a simple DC equivalent circuit. They do not model an unknown USB switch, powered protection rail, active GPIO, or boot comparator. Source limits: [DS Tables 3-1/3-13][ds]; [switch §5.5][mux]; [USB ECN][ecn].

An illustrative calculation uses VB=3.3 V ±5%, Rbias=4.7 kΩ ±1%, switch resistance 0-6 Ω, **zero assumed keyboard series resistance**, and **an assumed total 10 µA sink leakage**. The last two assumptions are not CK-5200 measurements. The 4.7 kΩ value comes from the WCH reference design; the tolerance and leakage budget are study assumptions. [WCH schematic p.4][sch]; [TS3USB221A §5.5][mux]

| Illustrative condition | Calculated result | Interpretation |
| --- | --- | --- |
| Host isolated, passive high-impedance target | Vnode minimum ≈3.087 V | Could exceed either cited GPIO threshold if rail and boot-input conditions hold |
| Host's 14.25 kΩ pulldown still attached | Vnode minimum ≈2.315 V | Below the 2.4 V GPIO threshold at VDD=5 V; no guaranteed assertion |
| Target or host drives low | Current upper estimate ≈0.745 mA | Does not establish allowable contention or injection current |
| Unpowered target, hypothetical clamp at 0.3 V | ≈0.680 mA through 4.653 kΩ | Illustrates possible back-power; clamp voltage and allowed current are not established |
| High-impedance target at VB maximum 3.465 V | Absolute bound requires VK ≥3.165 V | Necessary only; normal-operation limits and transient margin still apply |

For a later verified passive path, the maximum permissible series resistance follows from the high-level condition:

```text
R_max <= (VB_min - VH_required) / (IL_max + VH_required / RD_min)
where VH_required = VHboot + chosen margin.

With RD infinite: R_max <= (VB_min - VH_required) / IL_max.

If a manufacturer supplies a valid powered-off injection-current limit:
R_min >= (VB_max - Vclamp_min) / Iinjection_allowed.
```

**BLOCKED:** no accepted `Iinjection_allowed`, `Vclamp_min`, `VHboot`, or board leakage bound was established. Therefore the last expression cannot authorize a resistor value. The datasheet's 30/40 mA source/sink absolute ratings are not powered-off injection limits. [DS Table 3-1][ds]

#### Timing and pre-attach VDD

**[INFERENCE]** The required overlap is:

```text
latest time when VDD/path are safe for bias
  + maximum bias-switch delay
  + worst-case node rise/settling time
  + required setup time
< earliest BOOT sampling time

Bias must then hold through the latest sample plus hold time,
and be fully removed before any USB host connection/reset.
```

This cannot be derived from a nominal 17 ms POR delay. A first-order passive estimate would use `tau = (R || RD) * Cnode`, but board capacitance, switch startup, rail delay, and the boot sample window are unknown. A nominal RC calculation cannot bound this keyboard. [DS Tables 3-3/3-4, note 8][ds]

- **SUPPORTED HYPOTHESIS [INFERENCE]:** with the phone absent and bottom charging input unconnected, the batteryless keyboard should eventually lose power. That does not prove VDD=0 at a selected time or a guaranteed POR on the next attach. Capacitance, reverse paths, and CC/data leakage can retain or raise a rail. [Clicks battery FAQ][clicks]; [photo power-path limits][photo-report]
- **BLOCKED:** supplying the bottom charging port first might raise VDD before the adapter is attached, or might trigger the boot sample before bias can be applied. There is no evidence to select this as a workaround. It also creates a second possible VBUS source. [Clicks charging FAQ][clicks]; [unknown power topology][photo-report]
- **DISPROVEN (safe-by-resistor claim):** neither a 4.7 kΩ resistor nor a low-current 3.3 V source establishes safe pre-power assertion. The reference design follows local VDD; the adapter supply would not. [DS I/O maximum][ds]; [reference circuit][sch]
- **DISPROVEN (5 V safeguard claim):** `TPD2E2U06` is an ESD device, with 5.5 V reverse standoff and a specified transient clamp well above 3.3 V. It cannot be used as a steady-state 3.3 V clamp. Never route adapter 5 V directly to D+. [TPD2E2U06 §6.3/6.5, PDF p.4][esd]

### Gaps

**BLOCKED:** safe assertion voltage/time, allowable injection, VDD ramp and decay, pre-attach CC behavior, data-path availability, and recovery after adapter brownout. If valid passive Rd exists only after application startup, ordinary source attach may itself depend on the failed application. If VDD becomes safe only after BOOT sampling, this external-bias concept has no safe timing solution without a different verified entry method.

## 4. What would the conceptual BLE-to-USB-C adapter contain?

### Takeaway

**PROVEN (component capability):** ESP32-S3 provides BLE and a real USB host controller. **SUPPORTED HYPOTHESIS [INFERENCE]:** one module plus controlled power, CC handling, and physical data isolation can implement the adapter side. The target-side electrical gates are still **BLOCKED**. [ESP32-S3 datasheet §§4.2.1.7, 4.3][esp]; [host library][host]

### Cited Findings

- ESP32-S3 supports Bluetooth LE and USB OTG with host operation. ESPressif's host library supports full/low speed and control, bulk, interrupt, and isochronous transfers. The library exposes custom-client APIs, so a vendor-specific protocol need not be forced into a HID-only bridge. This proves transport capability, not WCH ISP implementation. [ESP datasheet PDF pp.55-56/63][esp]; [host "Features & Limitations", "Clients & Class Driver"][host]
- `ESP32-S3-WROOM-1-N8` is a documented module option. Module pin 13 is GPIO19/USB D−; pin 14 is GPIO20/USB D+. USB Serial/JTAG and OTG share the internal PHY, so debugging through that same internal PHY cannot be assumed independent of host operation. [Module Table 1-1, PDF p.3; pin table, PDF p.11][module]; [ESP §4.2.1.7][esp]
- The host library currently documents asynchronous transfers and no transfer timeouts. A recovery transport therefore needs bounded application supervision and a defined abort path. A BLE link is a command/data transport, not a replacement for USB signaling timing. [Host "Features & Limitations"][host]
- `TS3USB221A` offers a two-channel USB mux with a global disable. OE high disconnects both paths; selection must change while disabled. Its specified off-state leakage is up to ±1 µA and power-off leakage up to ±2 µA in stated conditions. This is finite isolation, not zero leakage or galvanic isolation. [TS3USB221A §§5.5, 5.8, 7.4][mux]
- `TPS25947` uses back-to-back FETs and reverse-current blocking. The comparison table identifies `TPS259470L` as the active-current-limit, adjustable-OVLO, latch-off variant. Its current threshold range starts at 0.5 A; the 0.5 A setting has 0.425-0.575 A bounds in the stated test conditions. Do not present this as a proven low-current keyboard limit. [TPS25947 p.1, §4 p.4, §6.5 p.9, §7.3.7 pp.35-36][efuse]
- `TUSB320LAI` supports a fixed DFP/source setting and CC attach/orientation detection. Non-failsafe pins can back-drive it when unpowered. The revised electrical table specifies 855/887/920 kΩ minimum/typical/maximum for VBUS_DET's external resistor, while older prose still says 900 kΩ. In fixed DFP-only use, its datasheet permits VBUS_DET to remain unconnected. [TUSB320LAI §5 p.3, §6.5 pp.5-6, §7.2.1.1 p.10, §7.2.5 p.12][cc]

Proposed manufacturer part numbers, for later design review only:

| Block | Candidate | Reason and unresolved qualification |
| --- | --- | --- |
| BLE + USB host | Espressif `ESP32-S3-WROOM-1-N8` | Native host and BLE; power budget, RF layout, watchdog behavior, and ISP client still need design [Module][module]; [Host][host] |
| Female USB-C mating port | GCT `USB4105-GF-A` | 16-contact USB 2.0 receptacle; exact case clearance, insertion depth, shell support, footprint, and current drawing must be checked [GCT product page][gct] |
| D+/D− host/bias selection | TI `TS3USB221ARSER` | Disable state and powered-off leakage specified; hardware default OE high; full signal-integrity/leakage budget required [Mux §§5, 7; ordering addendum][mux] |
| CC source detection | TI `TUSB320LAIRWBR` | Fixed DFP, default-current advertisement; pull configuration and power sequencing must follow its datasheet [CC §§5-9; ordering addendum][cc] |
| Switched 5 V with reverse blocking | TI `TPS259470LRPWR` | Latch-off for specified faults; set OVLO, slew, and current limits from a real budget; 0.5 A minimum may be too high [eFuse §4, §6.5, ordering addendum][efuse] |
| Independent 3.3 V rail | TI `TLV75733PDBVR` | 1 A LDO candidate; dissipation `(5-3.3)*I` and radio peaks require thermal review; not a keyboard VDD proxy [LDO §§5-7, ordering addendum][ldo] |
| Data-line ESD | TI `TPD2E2U06DRLR` | Low-capacitance ground-referenced ESD candidate, not overvoltage protection for MCU pins [ESD §§6-8, ordering addendum][esd] |
| Bias resistor | Unselected; 4.7 kΩ is only a study value | Cannot select until voltage, leakage, timing, and injection gates close [WCH reference][sch]; [DS][ds] |
| Hardware interlock/watchdog, VBUS sense and discharge | Unselected functional blocks | Need qualified power-off behavior, timing, leakage, fault latch, and discharge budget; missing items prevent a build-ready BOM |

The GCT product page explicitly lists `USB4105-GF-A` in its materials declarations. Its drawing download returned HTTP 403 in this investigation, so mechanical fit is not verified. Manufacturer ratings on that page do not authorize a matching power advertisement for the assembled adapter. [GCT][gct]

### Inferences

#### Annotated conceptual schematic

**[INFERENCE, proposed architecture]** Solid lines below are proposed adapter wiring. Every keyboard-internal edge marked `?` is unverified. No schematic symbol implies that an unselected safety block has been engineered or tested. This arrangement follows the documented component functions and USB pin assignments. [Type-C][typec]; [ESP module][module]; [Mux][mux]; [eFuse][efuse]; [CC][cc]

```text
Phone / BLE controller
        ~~~ BLE commands and buffered data ~~~
                         |
                 ESP32-S3-WROOM-1-N8
                 native USB OTG HOST
                  GPIO20 D+   GPIO19 D-
                       |          |
                       +---- A ---+  Host branch of dual USB mux
                                  \
Independent regulated 5 V           [TS3USB221A] ---- D+ ---- A6+B6
  |                                 /  common  ---- D- ---- A7+B7
  +--> 3.3 V LDO --> adapter rail   B                     |
  |       |                        |                     | ESD to GND
  |       +--> MCU / CC / mux       + D+ <- Rbias <- VB    |
  |                                + D- = open           |
  |                                                       |
  +--> TPS259470L reverse-blocking eFuse --> VBUS ----------+ A4/A9/B4/B9
          EN defaults OFF                    |             |
                                     protected sense      | FEMALE USB-C
                                     switched discharge   | RECEPTACLE
                                                         | mates with
  TUSB320LAI, fixed source                                | INNER MALE PLUG
       CC1 -----------------------------------------------+ A5
       CC2 -----------------------------------------------+ B5
       default USB current advertisement                  |
  Common GND ---------------------------------------------+ GND contacts

  Hardware interlock/watchdog:
    reset / fault / timeout / detach -> mux OE HIGH; eFuse OFF
    change mux S only with OE HIGH and a bounded dead interval
    enable bias branch ONLY if target-safe window is proven
    (target-safe predicate currently unavailable: BLOCKED)

  Keyboard internals, all electrical edges unknown:
    inner D+  ?--> flex ?--> protection/mux/series parts ?--> PC17/pin33
    inner D-  ?--> flex ?--> protection/mux/series parts ?--> PC16/pin32
    inner VBUS ?--> power switch/regulator ?--> U3 VDD/pin48
    inner CC  ?--> passive Rd / controller / PC14-PC15 ?
    bottom charging port ?--> charge-through/power circuit ?
```

SBU and SuperSpeed contacts are unused. CC1/CC2 are not shorted together. No phone occupies the male plug in this concept; the phone communicates over BLE. No charger is part of the target connection. An unexpected external VBUS source forces a fault rather than a source-role contest. Independent power means that adapter control remains available before keyboard power is enabled. [Type-C §§4.2-4.5][typec]; [Clicks charging behavior][clicks]

A fixed USB 2.0 source can omit VCONN for the restricted direct-sink case; a target requiring VCONN or a different charge-through role is outside this concept and blocks progression. TUSB320 is not a substitute for a USB-PD policy engine or a VBUS power switch. [Type-C §4.5.2.2.9.1, p.187][typec]; [CC functional description][cc]

#### Proposed fail-safe state sequence

This is an engineering state specification for later simulation/fixture work, not instructions to run against the keyboard. Bias timing values remain deliberately unselected. [DS][ds]; [Type-C][typec]; [Mux][mux]; [Host][host]

| State | Required electrical state | Transition requirement |
| --- | --- | --- |
| 0. Adapter unpowered/reset | eFuse disabled by hardware; mux disconnected; no intentional D+ bias | Off-state leakage/back-power budget must pass even if keyboard is externally powered |
| 1. Adapter ready | Independent supply stable; USB host physically isolated; fault latch clear | Local self-check only; BLE loss must not leave an indefinite bias |
| 2. CC attach | Advertise default source current; detect valid sink and orientation; VBUS initially off | Reject unsupported roles/accessories and unexpected VBUS; honor CC debounce |
| 3. Controlled power rise | Enable protected 5 V after valid attach; mux remains off | Known worst-case VBUS-to-VDD relation must establish target-safe timing |
| 4. Conditional BOOT bias | With mux off, select bias branch; then enable only inside the proven safe window | Independent bounded timer; no active host connection; abort if gates cannot be met |
| 5. Bias release | OE high removes bias and keeps host isolated | Wait qualified off/settling time; select host while still disabled |
| 6. Host connect | Enable host branch only after all added bias is disconnected | Target supplies its own normal USB attachment indication; normal host reset/enumeration can then occur |
| 7. USB transport | Native host handles transfers; BLE carries higher-level requests | Boot/protocol researcher must define permissible requests; no automatic erase, unprotect, or flash actions |
| Fault/detach/timeout | Disconnect data/bias first, then remove target source power; latch fault | Discharge only under qualified source-absent conditions; require an explicit new session, not automatic retries |

No step can bypass the missing target-safe predicate. A VBUS comparator alone cannot provide it. If PC17 must already be high before VDD is safe, states 3/4 cannot meet both requirements and the design stops there. [DS absolute limits and boot note][ds]

The mux's nanosecond switching specifications do not establish system dead time. Include control propagation, hardware interlock timing, regulator decay, stored charge, and worst-case temperature. Pull OE to the mux supply for disabled-by-default behavior; hold S at a defined value. On loss of adapter supply, rely only on qualified powered-off specifications, not an MCU GPIO state. Even ±2 µA leakage could lift a weakly loaded target rail over time. [Mux §§5.5/5.8/7.4/8][mux]

VBUS protection needs a real budget: available source current minus MCU/radio demand, cable/connector loss, target steady current, inrush, and fault response energy. `TPS259470L` latch-off does not mean every condition latches indefinitely; OVLO behavior can recover when voltage falls. A separate system fault latch is therefore a requirement. Discharge current must not fight an unexpected external source, and normal unattached leakage must meet the Type-C impedance requirement. [eFuse §§7.3.3, protection features][efuse]; [Type-C §4.4.2 Table 4-2, minimum 72.4 kΩ when not sourcing][typec]

### Gaps

**BLOCKED:** this is not a finished PCB or BOM. Hardware interlock, watchdog, discharge switch, sense circuit, CC transient protection, current budget, mechanical support, layout, and production tolerances need design review. Most importantly, the adapter cannot observe the internal VDD rail through the known USB pins. No single-module BLE/USB feature can resolve that missing target information.

## 5. What is feasible now, and what evidence would change the result?

### Takeaway

**SUPPORTED HYPOTHESIS [INFERENCE]:** a BLE-controlled native USB host adapter is technically plausible. **BLOCKED:** safe, application-independent CK-5200 recovery through that adapter is not established. The next useful step is model-matched electrical documentation, not a keyboard experiment. [Host capability][host]; [WCH electrical limits][ds]; [photo route limits][photo-report]

### Cited Findings

| Gate | Current status | Evidence needed to close it |
| --- | --- | --- |
| G1. Exact part/package | Package PROVEN; part SUPPORTED HYPOTHESIS | Existing manufacturer BOM or legible model-matched marking record [Photos][photos]; [DS][ds] |
| G2. Inner plug data reaches PC17/PC16 | BLOCKED | Existing schematic/netlist showing all flex, switch, resistor, and protection elements [Photo report §6][photo-report] |
| G3. Data path works before application startup | BLOCKED | Reset/default states and supplies for every intervening component [Photo route limits][photo-report] |
| G4. CC attach works with failed application | BLOCKED | Passive/dead-battery Rd path or independently operating controller [Type-C §§4.5/4.11][typec] |
| G5. Real power removal and bounded VDD rise | BLOCKED | VBUS-to-VDD schematic, discharge bounds, alternate-source behavior, existing timing captures [Clicks no-battery statement][clicks]; [DS][ds] |
| G6. Safe assertion window exists | BLOCKED | Boot research result plus G2-G5, VIH/setup/hold limits, leakage/injection qualification [DS][ds] |
| G7. Added bias can be removed before USB | Component capability PROVEN; system unverified | Break-before-connect state proof and later generic-fixture timing/signal checks [Mux][mux]; [USB 2.0][usb2] |
| G8. BLE + native USB host | PROVEN at component/API level | Later generic-device integration evidence for the chosen module and library [ESP][esp]; [Host][host] |
| G9. Recovery boot/protocol actually usable | BLOCKED in this electrical assignment | Separate boot research: entry semantics, immutable/remaining code, protection state, protocol and safe commands [Recovery audit][audit] |

### Inferences

**[INFERENCE, next step]** Obtain an existing CK-5200/iPhone-15-board-revision schematic, BOM, or manufacturer engineering statement covering the inner plug D+/D−/CC/VBUS, U3 VDD, and power-up behavior. Search existing public/local material first. A request to the manufacturer would require separate approval before sending; none was sent. The smallest useful evidence packet is:

1. Inner D+ to PC17 and D− to PC16 net names, all intermediate parts, and reset-time switch states.
2. U3 supply voltage, source path, alternate-source isolation, and worst-case ramp/discharge timing.
3. CC termination with VDD=0 and with application code unavailable.
4. Manufacturer-supported PC17 electrical threshold/setup/hold and powered-off behavior, interpreted with the boot researcher's findings.

These requests directly address G2-G6 rather than collecting more visually similar board photos. [Current photo limits][photo-report]; [WCH limits][ds]

**[INFERENCE, later separate work]** After the documentation gates close, a generic unrelated fixture could validate the adapter with existing equipment under separate approval. No purchase or construction is authorized here. A useful fixture would expose its own VDD and D+ so it can test two meaningful cases:

- Normal path: bounded supply ramp, bias within rail limits, complete bias removal, then native USB enumeration and a harmless known-device transfer.
- Key failure path: fixture supply absent/delayed or adapter brownout during bias; verify no out-of-limit pin voltage, rail back-power, unintended host drive, or automatic retry.

Offline circuit simulation can vary ramp, leakage, capacitance, tolerances, and fault sequencing before any fixture exists. Neither simulation nor a generic fixture proves the CK-5200 netlist or boot window. [DS limits][ds]; [Mux leakage/timing][mux]; [Type-C sequencing][typec]

### Gaps

No keyboard enumeration, device command, reset, probe, continuity test, power injection, or key experiment was performed. No hardware was purchased or built. No subagent was used for this assignment, and no commit or publication was made. Firmware builds/tests were not run because this deliverable changes research notes only. Verification consisted of photo/document inspection, manifest/PDF hashes, and offline divider/current calculations; there is no circuit-simulator or physical-test result to report.

## 6. Which sources and exact document versions support this study?

### Takeaway

The source set includes WCH documents, USB specifications, manufacturer component data, and local photo evidence. Every online source below was retrieved on **2026-10-16**. PDF page numbers above are physical PDF pages unless explicitly marked printed pages.

### Cited Findings

| ID | Source and exact locator | SHA-256 of retrieved PDF or archive |
| --- | --- | --- |
| DS | [WCH CH32X035DS0.PDF V2.3, pinned community mirror commit `74c247d6d518052316b35b68ddc82db2a70afc52`][ds]. WCH-authored, 47 pages. Pinouts PDF p.13; Table 2-1 pp.17-19; notes pp.20-21; electrical pp.25-29. | `1714c07d9448bc87ec667a602eba3e4c784a6ca7ca25ef125445df0ee4cc755d` |
| SCH | [WCH CH32X035SCH.pdf, official openwch repository commit `c6253942b29ff2a3f5e2829182a874afffa45881`][sch]. Seven pages; C8T6 sheet p.4 inspected graphically. | `06edb61f5546e9c0dcff9c4495f21cc549e50c3018fde387882965d31bd502c8` |
| TYPEC | [USB Type-C Release 2.4, October 2024, USB-IF/Promoter Group document hosted on TI][typec]. §§4.2-4.5, Tables 4-27/28, timing Tables 4-32/33. | `f5407e6120db1e28137e7fee60f3125f4284e2252b1240d271ce6e40c0ba3121` |
| USB2 archive | [USB-IF USB 2.0 specification archive, `usb_20_20250603.zip`][usb2]. | `5fe9c53c04033818af396e8852b3acbca5c3a76ba92fab549fd81cd0ea7b3692` |
| USB2 base PDF | `usb_20.pdf`, extracted from [USB2 archive][usb2]. §§7.1.5, 7.1.7.5. | `d39698a33486c399124af92bd02e4f978fd9a836b5cf4e52e6e4633eb1d89f61` |
| ECN | `resistor_ecn.pdf`, "Pull-up/pull-down resistors", extracted from [USB2 archive][ecn]. Summary p.1; replacement text pp.2-3; figure inconsistency p.4. | `7d54e297fd8b79706a37912a899d20dca08d01861b940f4ebe2c11498d492e51` |
| ESP | [ESP32-S3 Series Datasheet v2.2][esp]. USB §4.2.1.7 pp.55-56; Bluetooth p.63. | `2d5a7cb7fd559d8d972bd88db32669c0196d23f22d7afaafb0f63d099b589a3f` |
| MODULE | [ESP32-S3-WROOM-1 / WROOM-1U datasheet][module]. Ordering table p.3; pin assignments p.11. | `27d71971da07c280c6068d08c74720d1a25b8f20cf8494dc1765bdd28d40d435` |
| MUX | [TI TS3USB221A, SCDS277C, revised October 2024][mux]. §§5.3/5.5/5.8, function table §7.4, ordering addendum. | `cccebf8c10df605133d24bd3ab3abeb97a19dbf539d3465b53562fc19f7a242e` |
| CC | [TI TUSB320LAI, SLLSEQ8D, revised May 2017][cc]. Pin note §5 p.3; RVBUS §6.5 p.6; DFP §7.2.1.1 p.10; VBUS_DET §7.2.5 p.12; ordering addendum. | `62f7b3f65338e25ebd6fd46ef04474ba806b06b1cc04bc0b3d7b8191b01ca87d` |
| EFUSE | [TI TPS25947, SLVSFC9C, revised May 2026][efuse]. Comparison p.4; current limits p.9; protection §7.3; ordering addendum. | `8f96de389903091650d4f462dcfad3210071c3ae7093623a7978f34baf8a65b4` |
| LDO | [TI TLV757P][ldo]. Electrical/thermal §5; application §7; `TLV75733PDBVR` ordering p.25. | `bb9221408e3ea942efbab707b2a158aaf4bdfa8a15638cede1174d99d14aa60e` |
| ESD | [TI TPD2E2U06][esd]. Electrical p.4; application §8; ordering addendum. | `a133b86ea3d3c3d34667339b92d3b5e98d541d10e9775d63e7cdfa153794f4ed` |

HTML sources, with section locators rather than invented page numbers:

- [Clicks support][clicks]: "Does Clicks have a battery?", "Does Clicks use Bluetooth to pair with the iPhone?", "Can I charge my iPhone with Clicks?", and "Can I use wired CarPlay and transfer data with Clicks?" Scope is the keyboard-case FAQ, with the iPhone 15 paragraph used for data behavior.
- [ESP USB host library][host]: "Features & Limitations", "Host Library", "Clients & Class Driver", and "External PHY Configuration". The `latest` URL is mutable; these notes record its retrieved behavior, not a pinned SDK release.
- [GCT USB4105 product page][gct]: "Product overview", "Electrical", "Mechanical", and `USB4105-GF-A (revB)` materials declaration listing.
- [Local photo report][photo-report], [photo manifest][manifest], [X035 port report][port], [recovery audit][audit], and [recovery ADR][adr] are repository evidence, not new hardware observations.

### Inferences

**[INFERENCE]** The source hierarchy matters: a WCH evaluation board can prove a supported MCU circuit, but cannot prove that Clicks copied it. A USB standard defines required port behavior, but cannot prove compliance or implementation on this board. A TI datasheet proves component specifications only within its conditions.

### Gaps

The direct USB-IF Type-C R2.0 download returned HTTP 403; the retrieved Type-C source is the R2.4 TI-hosted copy identified above. The GCT drawing download also returned HTTP 403. No model-matched CK-5200 schematic was found in the examined material. PDF/source working copies and image derivatives are in ignored `tmp/r3-electrical/build/`; the durable deliverable is this file.

[ds]: https://raw.githubusercontent.com/ch32-riscv-ug/CH32X035/74c247d6d518052316b35b68ddc82db2a70afc52/datasheet_en/CH32X035DS0.PDF
[sch]: https://raw.githubusercontent.com/openwch/ch32x035/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32X035SCH.pdf
[typec]: https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/196/USB-Type_2D00_C-Spec-R2.4-_2D00_-October-2024.pdf
[usb2]: https://www.usb.org/sites/default/files/usb_20_20250603.zip
[ecn]: https://www.usb.org/sites/default/files/usb_20_20250603.zip
[esp]: https://documentation.espressif.com/esp32-s3_datasheet_en.pdf
[module]: https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf
[host]: https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html
[mux]: https://www.ti.com/lit/ds/symlink/ts3usb221a.pdf
[cc]: https://www.ti.com/lit/ds/symlink/tusb320lai.pdf
[efuse]: https://www.ti.com/lit/ds/symlink/tps25947.pdf
[ldo]: https://www.ti.com/lit/ds/symlink/tlv757p.pdf
[esd]: https://www.ti.com/lit/ds/symlink/tpd2e2u06.pdf
[clicks]: https://www.clicks.tech/pages/support
[gct]: https://gct.co/connector/usb4105
[photos]: ../../keyboards/ck5200/docs/hardware-photos/
[crops]: ../../keyboards/ck5200/docs/hardware-photos/crops/
[manifest]: ../../keyboards/ck5200/docs/hardware-photos/MANIFEST.sha256
[photo-report]: ../../keyboards/ck5200/docs/hardware-photo-report.md
[port]: ../../keyboards/ck5200/docs/X035-PORT.md
[audit]: ../../keyboards/ck5200/docs/recovery-audit-2026-10-14.md
[adr]: ../../keyboards/ck5200/docs/adr/0001-independent-iphone-recovery.md
[u3]: ../../keyboards/ck5200/docs/hardware-photos/crops/u3-marking-p19.jpg
[u3-21]: ../../keyboards/ck5200/docs/hardware-photos/crops/u3-marking-p21.jpg
[u3-22]: ../../keyboards/ck5200/docs/hardware-photos/crops/u3-marking-p22.jpg
[u3-24]: ../../keyboards/ck5200/docs/hardware-photos/crops/u3-marking-p24.jpg
[u7]: ../../keyboards/ck5200/docs/hardware-photos/crops/u7-marking.jpg
[u1]: ../../keyboards/ck5200/docs/hardware-photos/crops/u1-marking.jpg
[u5]: ../../keyboards/ck5200/docs/hardware-photos/crops/u5-marking.jpg
[q6-crop]: ../../keyboards/ck5200/docs/hardware-photos/crops/q6-marking.jpg
[p01]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-01.jpg
[p02]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-02.jpg
[p03]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-03.jpg
[p04]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-04.jpg
[p05]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-05.jpg
[p06]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-06.jpg
[p07]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-07.jpg
[p08]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-08.jpg
[p09]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-09.jpg
[p10]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-10.jpg
[p11]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-11.jpg
[p12]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-12.jpg
[p13]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-13.jpg
[p14]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-14.jpg
[p15]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-15.jpg
[p16]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-16.jpg
[p17]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-17.jpg
[p18]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-18.jpg
[p19]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-19.jpg
[p20]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-20.jpg
[p21]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-21.jpg
[p22]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-22.jpg
[p23]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-23.jpg
[p24]: ../../keyboards/ck5200/docs/hardware-photos/ck5200-photo-24.jpg
