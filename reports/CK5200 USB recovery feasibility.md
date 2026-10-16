# Evidence must precede CK5200 recovery hardware

## 2026-10-17 follow-up: outcome 2, exact vendor dependency

**No preservation-safe factory-ISP sequence is established. The next dependency is WCH's loader contract plus Clicks' storage/installer contract.**
This is outcome **2**, not proof that every factory-ISP implementation makes recovery impossible.
The existing hardware concept remains a bounded investigation, not an adapter proposal or build authorization.

- **PROGRAM differs from ERASE.** An A5 address field can express `0x2000`. It does not prove that A4 can skip `[0,0x2000)`.
  The [loader follow-up](../research_notes/CK5200%20USB%20recovery%20feasibility/isp-loader-followup.md) examines WCH's compiled implementation separately from `wchisp` CLI behavior.
- **WCH's compiled host has the same missing erase-start control.** Its A4 routine sends only a count and enforces a minimum of eight for family `0x23`.
  `IsEraseAllCFlash=false` still calls erase. `IAPStartAddr` supplies no X035 erase or program base in this implementation.
  Its normal programming flow also writes configuration before erase. That high-level flow cannot satisfy the untouched-options requirement.
- **The 40 destination bytes remain unclassified.** Fresh static analysis found 36 `0xFF` bytes in the final **staging** page.
  Staging includes a four-byte marker, so these are different ranges. The missing OEM copier prevents a destination-padding conclusion.
  See the [tail analysis](../research_notes/CK5200%20USB%20recovery%20feasibility/v122-tail.md).
- **The exact request is prepared.** The [technical inquiry](../research_notes/CK5200%20USB%20recovery%20feasibility/vendor-inquiry.md) asks for version-matched erase/program effects, protection rules, tail policy, and interruption behavior.
  It is ready to send and remains **UNSENT**.

R3 remains **UNMET**, and flashing remains **NO-GO**. The simulator's hypothetical capability grants no hardware permission.

**Evidence boundary:** these are facts about the hash-pinned WCH **host** implementation, not a recovered target loader.
Target acceptance of initial A5 offset `0x2000`, implicit page erase, final-buffer effects, and undocumented selective operations remain unknown.
No matching factory BOOT image was recovered. The current official WCH-LinkUtility download returned an error.
Thus the inspected high-level host paths are unsuitable, but universal factory-ISP infeasibility is not established.
Independent static reviews confirmed both the WCH host findings and the staging-tail distinction.
Their [recorded scope and limits](../research_notes/CK5200%20USB%20recovery%20feasibility/review.md#2026-10-17-focused-follow-up) do not include target execution.

## Original feasibility study, 2026-10-16

**The inner male USB-C plug is a plausible factory-BOOT entry route, but no safe CK-5200 recovery method is established.** WCH documents PC17/D+ high at power-up as a BOOT selector. The existing photos do not prove that the plug reaches that pin before application startup. An iPhone-controlled BLE adapter with its own USB host could avoid the iPhone's raw-USB API limit. However, the inspected ISP client programs from zero, and no proven operation restores only the application while preserving all unknown bytes. Read protection, mutable System FLASH, the final page's 40 unknown bytes, and interrupted writes remain separate blockers. **Obtain model-matched evidence first. Do not fabricate an adapter or risk the only keyboard to discover these facts.**

Research date: **2026-10-16**. Study base supplied by the coordinator: `d981763e6e96b143afd74ded9d11c72603e47039`, branch `feat/r3-usb-c-recovery-study`. This report supplies engineering evidence, not authorization. [flash-readiness.md](../keyboards/ck5200/docs/flash-readiness.md) remains the sole readiness authority: **R3 UNMET, custom flashing NO-GO**. M1, M2, and M3 are closed within their recorded scopes. Their accepted diagnostics and software evidence does not prove physical recovery.

The scope is research, existing artifacts, and offline models. No keyboard operation, enumeration, reboot, probing, opening, ISP command, unlock, purchase, or hardware build is authorized. There is one keyboard and no spare-purchase plan. A future adapter is now inside the conceptual scope, but not the execution scope. This report contains seven deliverables: boot evidence, electrical concept, preservation audit, alternatives, eight R3 criteria, lab boundaries, and the next evidence step.

The labels apply to individual claims. **PROVEN (scope)** means the cited document, image, source, or calculation establishes that limited fact. **SUPPORTED HYPOTHESIS [INFERENCE]** means evidence supports a conditional explanation. **BLOCKED [UNVERIFIED]** means a required fact is missing. **DISPROVEN (scope)** means evidence contradicts the named claim. There is no overall confidence score.

## 1. PC17 can select BOOT, but the keyboard path remains unknown

### Power-up entry is a chip fact, not a connector fact

**PROVEN (WCH documentation):** datasheet V2.3 identifies PC17 as the BOOT detection pin. High at power-up selects the BOOT area. The evaluation-board guide identifies PC16 as USB D− and PC17 as USB D+. It describes holding Download during power connection, then releasing it for ISP. This is evidence about WCH hardware, not a CK-5200 procedure. ([Datasheet, chapter 2 note 8, PDF p.21 / printed p.19](https://www.wch-ic.com/download/file?id=381), [EVT guide V1.3, §5, p.12](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32x035%20Evaluation%20Board%20Reference-EN.pdf))

**BLOCKED [UNVERIFIED]:** WCH does not specify a complete PC17 sampling window, setup/hold times, dedicated BOOT threshold, or release deadline in the inspected sources. General GPIO thresholds and POR delays do not supply those values. The datasheet gives typical POR/PDR thresholds of 1.8/1.78 V and POR delays of 4/17/24 ms minimum/typical/maximum. Those values do not prove when BOOT samples PC17. ([Datasheet, tables 3-4 and 3-13, PDF pp.26,29](https://www.wch-ic.com/download/file?id=381))

| Event or flag | Established behavior | Recovery limit |
| --- | --- | --- |
| POR/PDR | Power reset differs from NRST, watchdog, and software system reset | A true MCU power cycle is the documented external entry candidate. Connector VBUS interruption alone does not prove it. |
| USB bus reset | Sets a USB interrupt flag | It is not MCU power reset or proof of PC17 resampling. |
| `BOOT_MODE`, bit 14 | Selects BOOT versus user area after software reset, with `BOOT_AVA` | It requires executing code. It cannot be the sole trigger after total application failure. |
| `BOOT_LOCK`, bit 15 | Runtime lock flag | It is not proof of permanent loader preservation. |
| `BOOT_STATUS`, bit 13 | Reports current program source | It does not establish the next external startup outcome. |
| `BOOT_AVA`, bit 12 | Read-only initial-configuration status, 1 means BOOT startup | The complete interaction with PC17 and all reset sources is missing. |
| RDPR/USER/Data0/1/WRPR options | Documented option fields | No documented STM32-style `BOOT0`, `nBOOT0`, or ISP-disable option appears in this table. That absence does not prove every manufacturing configuration. |

Sources: [RM V1.9 §§3.2.1–3.2.2, 18.2.2, 20.3.4, 20.5](https://www.wch-ic.com/download/file?id=382), PDF pp.13,221,240,248–249. The official `SystemReset_StartMode()` unlocks the selector, changes bit 14, and locks it. **It does not itself reset the MCU.** The register table calls the key register `R32_BOOT_MODEKEYR` at `0x40022028`, while §20.3.10 prints `BOOT_MODEKEYP`. These names describe a documentation inconsistency, not two registers. ([Pinned driver, lines 742–764](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/SRC/Peripheral/src/ch32x035_flash.c#L742-L764), [RM, PDF pp.239,244](https://www.wch-ic.com/download/file?id=382))

**DISPROVEN (timing inference):** normal USB D+ high does not prove BOOT entry. WCH's normal device example enables the GPIO pull-up and USB controller during application initialization. That can occur after startup sampling. A permanent external pull-up changes that sequence and is not a validated solution. The documented peripheral pull-up modes also do not define an external recovery circuit. ([USB example, lines 121–162](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/USB/USBFS/DEVICE/CH372Device/User/ch32x035_usbfs_device.c#L121-L162), [RM §§8.3.2.4,18.2.2](https://www.wch-ic.com/download/file?id=382))

### Factory BOOT is separate, mutable System FLASH

**DISPROVEN (immutable-ROM claim):** the factory launcher is not immutable ROM. WCH places 3,328 bytes of System FLASH at `0x1FFF0000–0x1FFF0CFF`. It explicitly permits replacement through WCH-LinkUtility. The official BootAsUser example overwrites this region and removes its former boot-jump service. Its guide and code disagree on the required utility version, but both establish replacement capability. ([RM §§1.2.1,20.1, PDF pp.6,238](https://www.wch-ic.com/download/file?id=382), [BootAsUser guide V1.2, pp.1,3–4](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/BOOT%E5%8C%BA%E5%9F%9F%E4%BD%9C%E4%B8%BA%E7%94%A8%E6%88%B7%E5%8C%BA%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E.pdf), [example, lines 13–26](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/User/main.c#L13-L26))

The presumed Clicks installer at main-flash offsets `0x0000–0x1FFF` is a different region. Main flash physically starts at `0x08000000`. The address-zero boot alias can select Code FLASH or system memory, so equal-looking offsets do not identify equal storage. **SUPPORTED HYPOTHESIS [INFERENCE]:** intact factory BOOT could execute without the application or presumed Clicks installer. **BLOCKED [UNVERIFIED]:** this keyboard's System FLASH contents, loader revision, initial configuration, and protection state remain unknown. There is no evidence that Clicks replaced the loader, and no evidence that it retained it unchanged. ([RM §§1.2,20.1](https://www.wch-ic.com/download/file?id=382), [artifact audit](../keyboards/ck5200/docs/recovery-audit-2026-10-14.md#rechecked-artifacts))

WCH says user code cannot erase the BootAsUser area. This supports a limited boundary against ordinary application flash operations, not immunity to debug programming or all faults. WRPR3 bit 7 separately protects the 3,328-byte system region. Neither that setting nor the keyboard's actual protection is known. ([BootAsUser example](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/BootAsUser/User/main.c#L13-L26), [RM §20.5, PDF p.249](https://www.wch-ic.com/download/file?id=382))

### Existing photos constrain the package, not the hidden nets

**PROVEN (photo scope):** U3 has 48 leads, 12 per side. The upper part marking remains unreadable. The lower line best resembles `15PM`, not a usable WCH identifier. **SUPPORTED HYPOTHESIS [INFERENCE]:** firmware-family evidence plus the package supports CH32X035C8T6. The earlier 64-lead pin map is disproven for the photographed package. Pin-1 orientation remains unknown. The electrical researcher inspected all 24 JPEG views and checked all 64 manifest entries. This synthesis relies on that recorded review, not a new physical observation. ([Electrical notes §1](../research_notes/CK5200%20USB%20recovery%20feasibility/electrical.md#1-what-do-the-existing-photos-establish-about-the-mcu-and-board), [photo 09](../keyboards/ck5200/docs/hardware-photos/ck5200-photo-09.jpg), [photo 22](../keyboards/ck5200/docs/hardware-photos/ck5200-photo-22.jpg), [photo 24](../keyboards/ck5200/docs/hardware-photos/ck5200-photo-24.jpg), [manifest](../keyboards/ck5200/docs/hardware-photos/MANIFEST.sha256))

| Conditional C8T6 function | LQFP48 pin | What remains unknown on CK-5200 |
| --- | ---: | --- |
| PC16 / UDM, internally bonded PC11 | 32 | Inner-plug D− route |
| PC17 / UDP, internally bonded PC10 | 33 | Inner-plug D+ route and boot-time loading |
| PC18 / DIO | 34 | Connection to `SWD` pad |
| PC19 / DCK | 37 | Connection to `SWC` pad |
| PC14 / CC1 | 38 | Whether this board uses MCU CC |
| PC15 / CC2 | 39 | Whether this board uses MCU CC |
| GND / VDD | 47 / 48 | Pad continuity, actual supply, discharge and ramp |
| PA21 / RST alternate function | 7 | Any exposed independent reset route |

This is **PROVEN (C8T6 documentation)**, not a physical mapping of this unit. WCH warns against simultaneous output use on bonded PC10/PC17 and PC11/PC16. USB use requires the paired PC10/PC11 configuration to avoid conflict. ([Datasheet, PDF p.13, table 2-1 pp.17–19, note 4 p.20](https://www.wch-ic.com/download/file?id=381), [WCH schematic, C8T6 sheet p.4](https://raw.githubusercontent.com/openwch/ch32x035/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32X035SCH.pdf))

The photos show `MAIN`/`TYPEC` and `IP15-KB-TYPEC` flex labels, four `3V3 / SWD / GND / SWC` pads, and six unassigned pads. They show no readable BOOT/reset pad or recovery button in the visible area. Traces disappear under parts, solder, flex, or hidden layers. U1/U5/U6/U7 and Q6/Q7 do not have reliable part identifications. **No complete connector-to-PC17 or VBUS-to-VDD path is established.** Two older crop filenames are misleading: `q6-marking.jpg` does not depict the eight-lead Q6, and `u3-marking-p21.jpg` centers U5. Those filenames cannot establish component identity. ([Photos 03](../keyboards/ck5200/docs/hardware-photos/ck5200-photo-03.jpg), [07](../keyboards/ck5200/docs/hardware-photos/ck5200-photo-07.jpg), [14](../keyboards/ck5200/docs/hardware-photos/ck5200-photo-14.jpg), [21](../keyboards/ck5200/docs/hardware-photos/ck5200-photo-21.jpg), [electrical review and corrections](../research_notes/CK5200%20USB%20recovery%20feasibility/electrical.md#1-what-do-the-existing-photos-establish-about-the-mcu-and-board))

**[CORRECTION]** The older photo report left a hidden battery possible. Clicks' product FAQ says the keyboard contains no battery and draws phone power. That resolves the product-level battery question, not the rail timing question. Capacitors, leakage, or another attached source can still prevent a timely MCU power reset. The same FAQ distinguishes iPhone 15 charging support from iPhone 16 data passthrough. The bottom charging port is not an established recovery data path. ([Clicks FAQ, battery and wired CarPlay/data sections](https://www.clicks.tech/pages/support), [older photo report §§6–7](../keyboards/ck5200/docs/hardware-photo-report.md#7-power-reset-boot))

Public X035 logs prove generic ISP communication, not keyboard recovery. G8U6 logs show loader 02.40 with `4348:55e0` and 02.60 with `1a86:55e0`. Protected F8U6 parts also answer identification. A separate 02.60 trace ends with `Error: Verify failed, mismatch` despite earlier success messages. Reports of NRST failure or a 3–5 second availability window lack sufficient revision and timing evidence to choose adapter timing. ([Issue #38](https://github.com/ch32-rs/wchisp/issues/38), [issue #43](https://github.com/ch32-rs/wchisp/issues/43), [issue #68](https://github.com/ch32-rs/wchisp/issues/68), [PR #114](https://github.com/ch32-rs/wchisp/pull/114), [boot research §3](../research_notes/CK5200%20USB%20recovery%20feasibility/boot-entry.md#3-what-do-demonstrated-community-logs-prove-and-what-gates-remain))

## 2. A BLE bridge needs its own USB host and a proven electrical window

### The bridge changes which device owns USB

**PROVEN (Apple API scope):** an ordinary iPhone app has Core Bluetooth for BLE communication. `NSBluetoothAlwaysUsageDescription` is required for the stated modern-iOS use. ExternalAccessory requires an available accessory and supported protocol. USBDriverKit's documented host hardware includes macOS and M-series iPads, not iPhone. IOUSBHost does not establish an ordinary iPhone raw-USB route. A USB-C connector or an `Info.plist` protocol entry cannot create that permission. ([Core Bluetooth](https://developer.apple.com/documentation/corebluetooth), [Bluetooth usage key](https://developer.apple.com/documentation/bundleresources/information-property-list/nsbluetoothalwaysusagedescription), [Apple transport evidence](../keyboards/ck5200/docs/iphone-recovery-transport.md))

**SUPPORTED HYPOTHESIS [INFERENCE]:** the iPhone can send commands and image bytes over BLE to a separate adapter. The adapter, not the iPhone, must perform USB enumeration, control requests, and bulk transfers. A BLE-only controller, passive cable, USB-UART bridge, or device-only USB controller cannot provide that host role. The phone leaves the keyboard's inner plug free and does not electrically connect to it. BLE removes the direct raw-USB API obstacle, but does not establish keyboard entry, safe programming, or complete restoration.

**PROVEN (component/API scope):** ESP32-S3 supports BLE and USB OTG host operation. Its host library supports control and bulk transfers and custom clients. `ESP32-S3-WROOM-1-N8` exposes USB D− on GPIO19/module pin 13 and D+ on GPIO20/module pin 14. Shared internal PHY use prevents assuming independent USB Serial/JTAG debugging during host operation. The host library documents asynchronous transfers without transfer timeouts, so a recovery design needs bounded supervision. These facts support a candidate, not an implemented ISP bridge. ([ESP32-S3 datasheet §§4.2.1.7,4.3](https://documentation.espressif.com/esp32-s3_datasheet_en.pdf), [module tables, PDF pp.3,11](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf), [host library, Features & Limitations and Clients & Class Driver](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html))

### Annotated concept, not a wiring instruction

Every adapter connection below is **proposed [INFERENCE]**, even where a part's capability is proven. Every `?` edge is **BLOCKED [UNVERIFIED]** keyboard wiring. No value, timing, footprint, or circuit here authorizes fabrication. A hardware interlock, discharge circuit, and target-safe predicate remain unselected.

```text
 iPhone app
     | BLE commands/data
     v
 ESP32-S3-WROOM-1-N8, native USB OTG HOST
     D+ GPIO20 / D- GPIO19 --> mux branch A (host)
 safe bias VB -> Rbias ------> mux branch B D+ (bias)
                               mux branch B D- OPEN
                                        |
                             TS3USB221A common pair
                                        |
                          receptacle D+ A6/B6, D- A7/B7
                 OE HIGH = disconnected, select only while disabled

 Independent regulated 5 V
     +-> 3.3 V LDO -> adapter MCU / CC / mux supplies
     +-> reverse-blocking eFuse -> receptacle VBUS A4/A9/B4/B9
         EN defaults OFF, valid CC attach required before VBUS

 TUSB320LAI fixed SOURCE -> receptacle CC1 A5 and CC2 B5 separately
 Common GND -------------> receptacle GND contacts
 Data ESD ---------------> GND, not a steady-state voltage clamp

 All receptacle signals above belong to one FEMALE USB-C port.
 It mates only with the keyboard's INNER MALE plug.
                                            |
                         CK-5200 unknown internal edges
       D+ ?-> flex ?-> protection / series parts / switch ?-> PC17 (33*)
       D- ?-> flex ?-> protection / series parts / switch ?-> PC16 (32*)
     VBUS ?-> regulator / power switch ?-> MCU VDD (48*)
       CC ?-> passive Rd / powered controller / MCU CC pins ?
   bottom charging port ?-> alternate power / charge-through path ?

 * Package numbers apply only if the C8T6 hypothesis is correct.
 Fault/reset/detach: hardware disconnects bias/data and disables VBUS.
 Bias enable requires a proven target-safe window. That proof is absent.
```

USB 2.0 receptacle D+ uses A6/B6 and D− uses A7/B7. Same-polarity data contacts join at the receptacle, but CC1/CC2 remain separate. SBU and SuperSpeed contacts are unused in this concept. The source must detect a valid sink through CC before supplying VBUS. D+ high is not CC attach. The source must also remove VBUS on detach. A source-only Type-C controller is not a USB host, USB-PD policy engine, or power switch. A target requiring another role or VCONN falls outside this restricted concept. ([Type-C R2.4, table 3-1, §§4.4.2,4.5.1.1,4.5.2.2.9](https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/196/USB-Type_2D00_C-Spec-R2.4-_2D00_-October-2024.pdf), [TUSB320LAI §§7–8](https://www.ti.com/lit/ds/symlink/tusb320lai.pdf))

### VBUS control does not establish VDD control

**BLOCKED [UNVERIFIED]:** no evidence establishes dead-battery Rd, switch defaults, or inner-plug data continuity at zero application execution. If CC termination or a data switch requires application firmware, the proposed route fails that dependency. Normal typing proves a working runtime path, not an early DC path to PC17. A schematic must identify all flex, protection, resistor, switch, and supply elements. Photos cannot close those gaps. ([Electrical connectivity ledger](../research_notes/CK5200%20USB%20recovery%20feasibility/electrical.md#2-can-the-inner-male-usb-c-connector-expose-dpc17-before-boot-sampling))

**Never connect adapter 5 V directly to D+.** WCH specifies I/O absolute voltage from −0.3 V to VDD + 0.3 V. Its USB/PD VDD operating range is 3.0–5.3 V. A separate 3.3 V source is not safe merely because a resistor limits current. At VDD=0, that source can violate the voltage limit or back-power the target. The WCH C8T6 reference uses `VCC -> 4.7 kΩ -> Download switch -> PC17`, with VCC also supplying MCU VDD. That local-rail relationship is missing from an external adapter. ([Datasheet, tables 3-1 and 3-2, PDF pp.25–26](https://www.wch-ic.com/download/file?id=381), [reference schematic, p.4](https://raw.githubusercontent.com/openwch/ch32x035/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32X035SCH.pdf))

The host must be physically isolated during added bias. Its nominal 15 kΩ pulldown loads D+, and a USB reset actively drives SE0. Software intent alone cannot prevent contention after controller startup or reset. A mux can separate host and bias branches, but its leakage is finite. `TS3USB221A` specifies up to ±1 µA off leakage and ±2 µA powered-off leakage under stated conditions. Those currents still need a board-specific back-power budget. ([USB 2.0 §7.1.7.5 and resistor ECN](https://www.usb.org/sites/default/files/usb_20_20250603.zip), [TS3USB221A §§5.5,5.8,7.4](https://www.ti.com/lit/ds/symlink/ts3usb221a.pdf))

For a passive node, the offline model is `Vnode = (VB - IL*R)*RD/(RD+R)`. `R` includes bias and path resistance, `RD` includes all pulldowns, and `IL` represents other sink leakage. Using 3.3 V ±5%, 4.7 kΩ ±1%, switch resistance 0–6 Ω, assumed 10 µA leakage, and zero assumed board resistance gives these results. They are **PROVEN (arithmetic under assumptions)**, not keyboard specifications. ([Electrical calculation and sources](../research_notes/CK5200%20USB%20recovery%20feasibility/electrical.md#conditional-bias-calculations-not-selected-keyboard-values))

| Illustrative condition | Calculated value | Consequence |
| --- | ---: | --- |
| Host isolated, high-impedance target | Minimum about 3.087 V | Logic plausibility only. Boot threshold and rail safety remain unknown. |
| Minimum 14.25 kΩ host pulldown remains | Minimum about 2.315 V | Below the documented 2.4 V general-GPIO threshold at 5 V VDD. |
| Hypothetical 0.3 V clamp on an unpowered target | About 0.680 mA | Possible back-power, not a permissible injection rating. |
| Maximum bias 3.465 V, high impedance | VDD must be at least 3.165 V for the absolute bound | This is only a damage boundary, not a normal-operation design margin. |

The resistor ECN's normative text gives 14.25 kΩ minimum, while one figure says 14.75 kΩ. The calculation uses the normative value. No allowed powered-off injection current, board capacitance, BOOT threshold, or board leakage bound is established. The general source/sink current ratings do not supply an injection allowance. Thus neither a resistor value nor a safe delay can be selected from these calculations. ([USB resistor ECN, pp.1–4](https://www.usb.org/sites/default/files/usb_20_20250603.zip), [datasheet, tables 3-1,3-13](https://www.wch-ic.com/download/file?id=381))

The required timing condition is: **latest safe VDD/path time + switch delay + node settling + setup time < earliest BOOT sample**. Bias must remain through the latest sample plus hold time, then disappear before host connection or reset. A VBUS comparator cannot prove internal VDD or this overlap. If the safe electrical window starts after BOOT sampling, this adapter concept fails. Supplying the bottom charging port first is not an established workaround and creates another power-source risk. ([WCH timing limits](https://www.wch-ic.com/download/file?id=381), [electrical timing analysis](../research_notes/CK5200%20USB%20recovery%20feasibility/electrical.md#timing-and-pre-attach-vdd))

### Candidate BOM identifies capabilities, not a finished circuit

| Function | Exact candidate | Evidence and remaining qualification |
| --- | --- | --- |
| BLE and native USB host | Espressif `ESP32-S3-WROOM-1-N8` | Host/BLE capability proven. Firmware, RF, power peaks, watchdog, and ISP client unverified. [Module datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf) |
| Female USB-C mating connector | GCT `USB4105-GF-A` | USB 2.0 receptacle candidate. Drawing retrieval returned HTTP 403. Case clearance, footprint, insertion depth, and mechanical support unverified. [Product page](https://gct.co/connector/usb4105) |
| Host/bias data mux | TI `TS3USB221ARSER` | Disable function and leakage specified. OE must default high. Selection changes only while disabled. System isolation and signal integrity unverified. [§§5,7 and ordering addendum](https://www.ti.com/lit/ds/symlink/ts3usb221a.pdf) |
| Type-C source/CC | TI `TUSB320LAIRWBR` | Fixed DFP/source candidate with default-current advertisement. Unpowered non-failsafe pins can back-drive it. This is not a PD controller. [§§5–9 and ordering addendum](https://www.ti.com/lit/ds/symlink/tusb320lai.pdf) |
| Protected, switched 5 V | TI `TPS259470LRPWR` | Reverse blocking, active current limit, latch-off variant. Minimum setting is 0.5 A, with 0.425–0.575 A bounds under stated conditions. It is not a proven keyboard-safe current limit. [§4, §6.5, §7.3](https://www.ti.com/lit/ds/symlink/tps25947.pdf) |
| Adapter 3.3 V | TI `TLV75733PDBVR` | 1 A LDO candidate. Thermal loss `(5-3.3)*I` and radio peaks need review. Its output does not measure keyboard VDD. [§§5–7 and ordering table](https://www.ti.com/lit/ds/symlink/tlv757p.pdf) |
| Data ESD protection | TI `TPD2E2U06DRLR` | Low-capacitance ESD candidate. Its 5.5 V reverse standoff does not clamp a steady D+ fault to 3.3 V. [§§6–8](https://www.ti.com/lit/ds/symlink/tpd2e2u06.pdf) |
| Bias, interlock, watchdog, discharge, sensing, CC protection | **Unselected** | No exact values or parts can close unknown target limits. These missing blocks make the BOM incomplete. |

TUSB320LAI has a documented VBUS_DET resistor discrepancy: the revised table gives 855/887/920 kΩ while older prose says 900 kΩ. Fixed DFP-only use permits that pin unconnected. The eFuse's OVLO can recover when voltage falls, so its latch-off name does not replace a system fault latch. Source capacity, radio demand, inrush, connector loss, fault energy, and discharge behavior require a real budget. No power rating in this table authorizes a Type-C current advertisement. ([TUSB320LAI §§6.5,7.2.5](https://www.ti.com/lit/ds/symlink/tusb320lai.pdf), [TPS25947 §7.3](https://www.ti.com/lit/ds/symlink/tps25947.pdf))

| Conceptual state | Required condition | Stop condition |
| --- | --- | --- |
| Reset/off | VBUS disabled by hardware, mux disconnected, no bias | Unqualified powered-off leakage or unexpected external VBUS |
| Ready/attach | Stable independent adapter supply, valid source-to-sink CC attach, host isolated | Firmware-dependent Rd, unsupported role, or unknown attach behavior |
| Power rise | Controlled VBUS after valid attach | No proven VBUS-to-VDD timing bound |
| BOOT bias | Isolated host, proven voltage/setup/hold window, bounded independent timer | Missing target-safe predicate, rail delay, contention, timeout |
| Release | Disable mux, remove bias, wait qualified settling, select host while disabled | Residual bias or unknown switch behavior |
| Host operation | Target provides normal attachment indication, then host enumerates | Unknown identity, protocol revision, protection, or status |
| Fault/detach/BLE loss | Hardware removes data/bias and source power in a qualified sequence, then latches fault | No automatic retry, unprotect, reset-on-exit, or discharge into another source |

This table is a specification for later review and simulation. It is not a keyboard procedure. BLE loss must not leave indefinite bias. Recovery from loss of phone control also cannot depend on phone scheduling. A bounded local operation and persistent uncertainty record would be needed before any future write path. No such implementation is claimed.

## 3. Entry does not solve app-only restoration or preservation

### The stock file omits required bytes

The archived V122 image has **18,648 bytes**, SHA-256 `8ee86935f5fbd622972fa55033f29fa551ba572288123046e21d79006f3b22f8`. It spans linked offsets `[0x2000,0x68D8)`, ending at `0x68D7`. It contains no original bytes below `0x2000`. The prior audit checked the artifact and linked reset jump to `0x485C`. This report reuses that evidence. It does not claim a new image read from the keyboard. ([Artifact audit](../keyboards/ck5200/docs/recovery-audit-2026-10-14.md#rechecked-artifacts))

| Storage | Required treatment |
| --- | --- |
| Main offsets `[0x0000,0x2000)` | Unknown presumed OEM installer. Never erase or program it. A later rewrite would fail the untouched requirement. |
| Main offsets `[0x2000,0x68D8)` | Known V122 replacement bytes, subject to all entry, programming, and authorization gates. |
| Page-rounded `[0x2000,0x6900)` | 73 pages of 256 bytes. The final 40 bytes `[0x68D8,0x6900)` are unknown and remain protected. |
| Nominal application `[0x2000,0x8A00)` | 106 pages. This capacity is not permission to erase unused-looking bytes. |
| All other main flash through `0xF7FF` | Preserve unless exact evidence authorizes a specific change. Includes staging and reserved bytes. |
| Main pages `0xF600–0xF6FF`, `0xF700–0xF7FF` | Preserve complete serial/configuration pages, not only the observed fields. |
| System FLASH `[0x1FFF0000,0x1FFF0D00)` | Separate 3,328-byte loader. Preserve it and establish its actual identity. |
| Manufacturer configuration `[0x1FFFF700,0x1FFFF800)` | Separate factory-locked region. It is not main-flash offset `0xF700`. |
| User options `[0x1FFFF800,0x1FFFF900)` | Preserve protection and configuration. No generic reset or unlock. |
| ESIG UID and external authentication state | Separate identities/state. A successful code comparison cannot establish their preservation. |

The 62 KiB main-memory map contains 248 pages. Protection groups contain eight pages, or 2 KiB. The read-only ESIG UID uses `0x1FFFF7E8`, `0x1FFFF7EC`, and `0x1FFFF7F0`. The serial formatter's use of `0x0800F600` does not identify every device-specific byte. Unknown storage remains unknown, not assumed erased. ([RM chapters 19–20, PDF pp.236–249](https://www.wch-ic.com/download/file?id=382), [audit, dynamic serial](../keyboards/ck5200/docs/recovery-audit-2026-10-14.md#usb-control-endpoint-not-the-companion-dispatcher), [preservation research](../research_notes/CK5200%20USB%20recovery%20feasibility/isp-preservation.md#memory-that-must-survive))

### The inspected client directly violates the required destination

**DISPROVEN (current-client suitability):** `wchisp` commit `553327eeeea9fdc30414f1e5c74d1ff14b228a26` cannot perform this preservation-safe restore. Its first A5 program address is zero. That alone disqualifies it, regardless of unresolved loader erase behavior. ELF/HEX conversion subtracts the lowest section address, so linking a file at `0x2000` does not preserve its destination in this path. ([`flashing.rs`, lines 208–228 and 262–278](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/flashing.rs#L208-L278), [`format.rs`, lines 173–188](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/format.rs#L173-L188))

| Inspected source fact | Exact implication |
| --- | --- |
| A4 encodes `a4 04 00` plus `sectors:u32 LE` | It contains a count, not a start address or page list. |
| X035 host minimum is eight 1 KiB sectors | The client raises smaller requests to eight. This does not prove every loader has that minimum. |
| CLI zero-pads this image to 19,456 bytes (`0x4C00`) | It programs the padded bytes from zero, not the image at `0x2000`. |
| CLI requests `padded_length/1024 + 1` sectors | This image requests 20 sectors. Under the conventional prefix model, that means `[0,0x5000)`. |
| A5 and A6 each initialize address to zero | Programming and comparison both target the wrong start. |
| A5 uses up to 56 data bytes and an empty final packet | Finalization and page effects need separate target-side proof. |
| No-erase mode skips A4 only | It still programs at zero. Padding with zero or `0xFF` is not preservation. |

Sources: [A4/A5/A6 serializers](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/protocol.rs#L168-L204), [minimum/count handling](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/flashing.rs#L456-L465), [device policy](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/device.rs#L216-L243), [CLI padding and erase count](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/main.rs#L227-L269).

**SUPPORTED HYPOTHESIS [INFERENCE]:** A4 is a zero-based prefix erase in the conventional client model. The count-only payload and zero-based programming support that interpretation. **BLOCKED [UNVERIFIED]:** host code does not prove every loader's internal start, protected-page handling, count limits, end clamping, or undocumented operations. Thus the report does not turn a source inference into a universal silicon claim. The packet has no established safe application-range encoding. Changing A5 to `0x2000` would not resolve A4, implicit writes, protection, or the final-page tail.

**PROVEN (source defect):** the client's generic response parser returns `Ok` for correctly sized packets through an `if true` path. It does not correlate the reply opcode or establish operation success. A4/A5 use that weak result. X035 protection detection also omits type `0x23` from `support_code_flash_protect()`, although the YAML decodes RDPR. No warning is not proof of an unprotected target. The normal Flash arm does not call `unprotect()` despite a misleading summary comment. Explicit configuration flows are separate hazards. ([Parser, lines 316–327](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/protocol.rs#L316-L327), [initialization and unprotect](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/flashing.rs#L26-L203), [device policy](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/device.rs#L240-L243), [CLI](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/main.rs#L227-L366))

### Compare is not readback, and unlock is destructive

**DISPROVEN (backup claim):** A6 receives expected bytes and compares them on the device. It does not return unknown main-flash bytes. A7 returns configuration/version/UID, not arbitrary memory. AB is a data-flash read operation, and the X035 profile has no EEPROM size. None is an established reader for the unknown installer or the 40-byte tail. ([Protocol](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/protocol.rs#L191-L224), [EEPROM path](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/flashing.rs#L380-L420), [retracted readback assumption](https://github.com/ch32-rs/wchisp/issues/77#issuecomment-2571372879))

**PROVEN (WCH protection rules):** RDPR `0xA5` with its valid complement indicates unprotected operation. Other values mean protected operation. In the protected state, system-boot, SDI/debug, and RAM execution cannot erase/program main memory except through whole-chip erase. Releasing read protection automatically erases the user area. The first eight main-flash pages receive automatic write protection, totaling only 2 KiB, not the entire presumed 8 KiB installer. **BLOCKED:** the keyboard's RDPR/WRPR state is unknown. A protection change cannot be a preservation-safe prerequisite. ([RM §§20.2.2,20.5.4, PDF pp.238,250](https://www.wch-ic.com/download/file?id=382))

The 02.60 F8U6 trace contains A4 `a4 04 00 08 00 00 00`, then A6 reply `a6 00 02 00 f5 00`. The comparison failed. Earlier ACK-like replies do not prove a physical erase or write succeeded. Preserve `f5 00` as the observed failure payload, not an invented universal status dictionary. The trace's old 64 KiB display conflicts with WCH's current 62 KiB map. ([Issue #68](https://github.com/ch32-rs/wchisp/issues/68), [RM §20.1](https://www.wch-ic.com/download/file?id=382))

The official `WCHISPTool_CMD.ZIP` supplies wrapper source, public headers, and compiled libraries, not library implementation source. Its header exposes no main-flash dump, addressable page erase, or SRAM load-and-execute API. Its wrapper can clear data flash before the program/verify branch when `IsClearDataFlash` is enabled. Therefore even an operation called verify is not safe to run speculatively. No library or tool was executed. ([Official archive](https://www.wch-ic.com/download/file?id=365), [exact members, hashes, and source locators](../research_notes/CK5200%20USB%20recovery%20feasibility/isp-preservation.md#source-pins-and-exact-official-package))

**2026-10-17 compiled-library findings:** static analysis now resolves what the named settings do in this host implementation.
`CH55xIsp_Erase` sends seven bytes: one A4 opcode byte, two payload-length bytes, and a four-byte count.
It accepts no start address. `IsEraseAllCFlash=false` still calls it, requesting 19 units for V122 rather than 62 for the whole configured flash.
Those numbers differ from `wchisp`'s 20-unit policy and remain host requests, not independently measured erase footprints.
The X035 configuration branch does not use `IAPStartAddr`. The normal programming flow writes configuration, erases, then programs from zero.
The low-level A5 serializer accepts an address without calling host ERASE, but target-side implicit erase and initial-offset acceptance remain unknown.
See [symbols, instruction addresses, binary hash, and search limits](../research_notes/CK5200%20USB%20recovery%20feasibility/isp-loader-followup.md).

### A RAM helper needs an entry route that is absent today

**PROVEN (MCU capability):** code already executing with suitable access can erase/program 256-byte pages. **BLOCKED [UNVERIFIED]:** neither inspected factory-ISP interface provides a documented SRAM upload and arbitrary branch. A2 ends ISP or resets. It is not a jump-to-address command. SDI's ability to use RAM does not create a USB-loader command, and RAM execution does not bypass RDPR restrictions. ([RM §§20.2.2,20.4.4–20.4.5](https://www.wch-ic.com/download/file?id=382), [official page example](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/FLASH/FLASH_Program/User/main.c#L24-L98), [factory protocol enum](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/protocol.rs), [WCH two-wire debug configuration](https://github.com/openwch/openocd_wch/blob/e39efea3b937f4f195bc27b5c08ba75c6123f44a/scripts/interface/wch2pin.cfg#L1-L3))

Installing a helper in application flash before obtaining independent recovery repeats the rejected patched-stock bootstrap. A hypothetical volatile helper would need a no-flash-write loader route, documented access rights, exact bounds, and power-loss behavior. Its RAM contents disappear on power loss, so repeated safe entry and reload would remain necessary. There is no basis for probing unknown commands on the only keyboard. ([Existing ADR, reader decision](../keyboards/ck5200/docs/adr/0001-independent-iphone-recovery.md#3-readback-and-the-patched-stock-proposal))

The optional WCH IAP example is also not factory ISP or the Clicks installer. It uses base `0x08005000` and commands `0x80–0x84`. Its end operation erases the page containing `0x0800F800-4`, which is main-flash page `0xF700`. Its verify path can flush buffered programming. Copying that example unchanged would violate the preservation requirement. ([IAP header](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/IAP/USB_UART/CH32X035_IAP/User/iap.h#L19-L35), [IAP implementation](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/EXAM/IAP/USB_UART/CH32X035_IAP/User/iap.c#L18-L133))

### Power-loss survival needs a durable independent service

**BLOCKED [UNVERIFIED]:** there is no version-matched contract for page atomicity, partial writes, durable ACKs, safe retries, or recovery after every interruption. Lost replies can mean either no operation or an operation completed without a reply. Separate erase/program/compare steps are not an atomic installation. A host journal records uncertainty but cannot repair an unknown installer or recreate lost identity bytes. ([Client flow](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/main.rs#L227-L269), [existing manager journal limits](../keyboards/ck5200/docs/stock-manager-readiness.md#implementation))

**SUPPORTED HYPOTHESIS [INFERENCE]:** an intact independently selectable factory loader could allow a fresh restore after power loss without requiring application rollback. That would satisfy the availability part only if entry, power sequencing, and selective operations remain valid after each partial failure. It must still preserve every excluded region during success and failure. Neither enumeration nor a successful known-image comparison proves those properties or the external authentication state.

**BLOCKED [UNVERIFIED]:** direct application programming also leaves the original startup and staging state unresolved. **[INFERENCE]** a retained staging marker could cause the OEM installer to copy another image after factory recovery. The original marker consumption, validation, clearing, and copy rules remain unknown. Restoring V122 bytes therefore does not prove that the next normal boot retains and executes them. Clearing an unknown marker speculatively would add another unproven mutation. ([Installer audit and staging limits](../keyboards/ck5200/docs/recovery-audit-2026-10-14.md#what-the-restore-test-actually-proves), [ADR interruption gaps](../keyboards/ck5200/docs/adr/0001-independent-iphone-recovery.md#1-evidence-and-the-execution-boundary))

## 4. The alternatives fail different requirements

The table evaluates each route against the user's constraints. **B** is the new conceptual route. **E** is the highest-value evidence route today. A manufacturer's undocumented method is not assumed to exist.

| Criterion | A. Existing app/M2/M3 | B. Inner-plug BLE/USB host interposer | C. Other independent factory entry | D. PCB/SDI, comparison only | E. Manufacturer-supported method |
| --- | --- | --- | --- | --- | --- |
| Entry with application completely unbootable | **DISPROVEN** for demonstrated app dispatch/startup | **SUPPORTED HYPOTHESIS**, if PC17 and intact BOOT are reachable | **BLOCKED**, no other exposed independent CK-5200 trigger established | **SUPPORTED HYPOTHESIS** at debug-interface level, actual route/protection unknown | **BLOCKED**, needs model-matched method |
| No opening, pads, solder, or probing | **PROVEN** for normal app transport, not independent recovery | **SUPPORTED HYPOTHESIS**, female mate on inner male plug | **BLOCKED**, no qualifying exposed route known | **DISPROVEN** for a pad-access method under this constraint | **BLOCKED**, depends on vendor method |
| iPhone-operated, no computer during recovery | **PROVEN** for accepted stock diagnostics only. Restore remains host-tested | **SUPPORTED HYPOTHESIS**, phone BLE controls real adapter host | **BLOCKED**, UART or USB entry alone supplies no phone transport | **BLOCKED**, no approved iPhone/probe workflow | **BLOCKED**, must name the supported phone app/transport |
| No dependency on stock iAP2/authentication during entry | **DISPROVEN** for demonstrated app path | **SUPPORTED HYPOTHESIS**, BLE plus factory USB bypasses app session | **BLOCKED**, exact service unknown | **SUPPORTED HYPOTHESIS** for independent debug, not authorized | **BLOCKED**, must document independence |
| Preserve unknown installer, config, serial, options, auth | **BLOCKED**, original installer effects unknown | **BLOCKED**, current client unsuitable, no safe selective primitive | **BLOCKED**, alternate entry does not fix programming semantics | **BLOCKED**, read protection and tool side effects unknown | **BLOCKED**, requires untouched-region contract |
| Complete known official image restored | **PROVEN** image preparation only. Physical installation **BLOCKED** | **BLOCKED**, offset, erase, tail, status semantics missing | **BLOCKED**, same restoration gaps | **BLOCKED**, method/data absent | **BLOCKED**, requires complete model-matched artifact and validation |
| Survive loss of power at every stage | **BLOCKED**, host tests do not execute OEM installer | **SUPPORTED HYPOTHESIS** for retained loader, full requirement **BLOCKED** | **BLOCKED**, no complete state model | **BLOCKED**, no demonstrated procedure | **BLOCKED**, requires interruption evidence |
| No spare keyboard purchase | **PROVEN** existing software work needs none | **PROVEN** conceptual research needs none. Hardware remains unauthorized | **PROVEN** document research needs none | **PROVEN** comparison needs none. Probe action remains excluded | **PROVEN** an unsent query needs none |
| Credible proof before risking the only keyboard | **BLOCKED** for independent recovery | **BLOCKED**, needs vendor/target evidence and later generic fixture | **BLOCKED**, no independent route evidence | **DISPROVEN** as a way to learn by risking this unit first | **SUPPORTED HYPOTHESIS**, existing vendor records could avoid that circularity |

A's execution dependency is established by application callbacks, reset startup, and M2's accepted software scope. M3's stock-to-stock and QM/RS-to-stock workflows use host models. They do not execute the original installer. Reopening M1 diagnostics or implementing another app rescue branch cannot answer the independent-entry question. ([Recovery audit](../keyboards/ck5200/docs/recovery-audit-2026-10-14.md#why-the-current-update-paths-cannot-rescue-a-dead-application), [M2 acceptance](../keyboards/ck5200/docs/application-rescue.md#acceptance), [M3 scope](../keyboards/ck5200/docs/stock-manager-readiness.md#scope-and-capability-boundaries))

C includes the documented USART2 factory transport on PA2 TX/PA3 RX. WCH still requires PC17 high at power-up, and no non-invasive CK-5200 route to those UART pins is established. A key chord handled by application code, software BOOT selection, NRST assumptions, or the bottom charge port does not supply independent entry evidence. D could supply forensic data under other constraints, but would not meet this non-invasive product requirement. No SDI research or operation resumes through this report. ([EVT guide §5](https://github.com/openwch/ch32x035/blob/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32x035%20Evaluation%20Board%20Reference-EN.pdf), [key-chord audit](../keyboards/ck5200/docs/recovery-audit-2026-10-14.md#reported-123--space-combination), [photo limits](../keyboards/ck5200/docs/hardware-photo-report.md#4-the-four-pads-beside-u3-and-the-sdi-hypothesis))

## 5. Eight falsifiable R3 criteria define the missing proof

These criteria refine the investigation. They do not create another readiness authority or authorize experiments. Each criterion needs evidence for the actual board/loader revision. A generic-chip result closes only its stated generic scope. **All eight remain unmet for the full keyboard recovery claim.**

| ID | Acceptance criterion | Observation that defeats the proposed route | Evidence needed before risking the only keyboard |
| --- | --- | --- | --- |
| R3-1 Independent selection | An external, non-invasive action enters a known recovery service when the entire application is invalid, including erased/corrupt vectors, with no app execution. The service also survives failed application installation. | Entry needs application code, a prior software flag, or a replaced/missing BOOT service. | Model-matched factory configuration and loader identity, entry specification, and existing vendor evidence for dead-app entry. |
| R3-2 Connector and CC independence | Exact inner-plug D+/D−/CC routes and every intermediate default state permit entry and USB operation before application startup. | Gated data, absent zero-power Rd, wrong connector, unsupported source/sink role, or required VCONN outside the design. | Schematic/netlist, BOM, flex mapping, and reset-state specifications for hardware `1.2.0`. |
| R3-3 Safe power and bias window | Proven bounds establish real VDD reset, safe D+ voltage, BOOT setup/hold, complete bias release, and no host contention/back-power across tolerances and faults. | VDD remains powered, bias exceeds limits, no timing overlap exists, or the host drives while bias is connected. | Vendor timing/electrical limits, rail topology and existing captures, then separately authorized generic-fixture validation. |
| R3-4 iPhone-operated transport | The iPhone controls a real host over a supported public API without the failed keyboard's iAP2 session. Local control handles phone/BLE loss within bounded times. | A device-only bridge, missing host stack, required raw-USB entitlement on iPhone, or unsafe dependence on BLE timing. | API and component evidence plus an offline protocol design, then generic host/BLE integration evidence under separate authorization. |
| R3-5 Exact stock placement and validation | The complete pinned 18,648-byte V122 reaches `[0x2000,0x68D8)` through version-specified offset, status, flush, and compare behavior. Invalid images fail before mutation. Normal OEM startup retains and executes restored stock with its identity/authentication intact. | Destination normalization to zero, unknown status accepted as success, incomplete image, unbounded finalization effects, or stale staging that replaces restored stock. | Loader and OEM startup contracts/source, exact image hash, validated packet semantics, and target-independent regression evidence. |
| R3-6 Untouched non-app state | Every effect stays inside an explicitly approved mutation set. Installer, unknown tail, other main flash, options, System FLASH, identity, and auth remain unchanged throughout success and every failure. No unlock or config reset is required. | Prefix erase, RDPR release, even temporary tail erasure, implicit write outside bounds, or external state mutation. | Exact erase/program footprint, full storage inventory, protection state, and a non-destructive preservation argument covering success and failure. A backup followed by rewrite does not meet the untouched requirement. |
| R3-7 Recovery after every interruption | Power/BLE loss before, during, and after erase/program/verify leaves independent entry available. A subsequent documented attempt can complete stock restoration without non-app changes. | Loader becomes unreachable, partial effects escape bounds, retry depends on corrupt app state, or unknown replies trigger blind retries. | Version-matched persistence model, interruption points, durable-completion rules, and existing vendor or later generic-fixture fault evidence. |
| R3-8 Evidence before unique-unit risk | Independent review accepts applicable vendor/source evidence and bounded validation before any first keyboard experiment. No spare purchase, replacement boot install, or exploratory unlock is needed. | The plan's first proof requires risking the sole unit or assuming generic-chip results establish CK-5200 wiring. | A complete evidence packet for R3-1 through R3-7, explicit residual limits, and a separate user decision. |

R3-1 is plausible because BOOT resides separately. R3-6 is a harder current blocker because the available host client directly targets protected-by-policy bytes. A successful future enumeration would strengthen R3-1/R3-4 only. It would not authorize A4, A5, A8, reset, or any restoration attempt. The 40-byte tail alone defeats an ordinary last-page erase under the strict unchanged requirement. Learning its contents and rewriting them later would not preserve it throughout a power cut. The lab therefore requires a stronger hypothetical operation before any mutation, as explained below. ([Boot research](../research_notes/CK5200%20USB%20recovery%20feasibility/boot-entry.md), [preservation conditions](../research_notes/CK5200%20USB%20recovery%20feasibility/isp-preservation.md#minimal-extension-evidence-required), [tail counterexample test](../keyboards/ck5200/recovery_lab/test_lab.py#L139-L147))

## 6. The offline lab can reject unsafe assumptions, not prove recovery

**PROVEN (local software checks, 2026-10-16):** all 10 unit tests passed after inspection of [lab.py](../keyboards/ck5200/recovery_lab/lab.py), [test_lab.py](../keyboards/ck5200/recovery_lab/test_lab.py), and [README.md](../keyboards/ck5200/recovery_lab/README.md). The default CLI returned expected exit **2**, with restoration blocked. The explicit hypothetical scenario returned **0**, with matching image and strict-region model checks. A Python subprocess check captured both outputs and asserted those exit codes and boundary messages. Neither result proves OEM normal boot, physical preservation, or R3.

The lab uses only the Python standard library and the existing V122 resource. It has no USB/BLE transport, enumeration, device access, ISP tool execution, or firmware dependency. The whole modeled application `[0x2000,0x8A00)` starts invalid. Independent entry and intact BOOT are explicit assumptions, not CPU execution or measured recovery behavior. The source-derived identity fixtures cover only the named generic G8U6/02.40, G8U6/02.60, and F8U6/02.60 traces. They do not identify the keyboard or validate the C8T6 hypothesis. ([Lab identity and initialization](../keyboards/ck5200/recovery_lab/lab.py#L91-L164))

The source supports a version-tagged packet model. It does not support an executable preservation-safe programmer. Factory ISP requests use an opcode, `u16 LE` payload length, and payload over vendor bulk endpoints. The pinned client accepts `4348:55e0` and `1a86:55e0`, configuration 1/interface 0, OUT `0x02`, IN `0x82`. Those values identify a host policy and generic examples, not this keyboard. The X035 family uses device type `0x23`. The C8T6 entry uses chip ID `0x51`, but the keyboard's actual reply is unknown. ([USB transport source](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/transport/usb.rs#L9-L188), [device database](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/devices/0x23-CH32X03x.yaml#L1-L92))

| Item | Implemented behavior and limit |
| --- | --- |
| A1 identify | Strict reply fixtures check USB IDs, chip/type order, and pinned generic chip/loader/capacity combinations. This is not enumeration. |
| A3 session key | **Not implemented.** Source research describes XOR/key behavior, but the lab does not derive or negotiate keys. |
| A7 configuration | **Not implemented.** Identity/protection fields are fixture inputs, not parsed device configuration. No all-config or memory-read service exists. |
| A4 erase | Decodes a count-only packet into a conservative prefix model. Count 20 gives `[0,0x5000)`. Bounds are model policy, not target proof. Execution is rejected. |
| A5 program | Decodes actual source-derived framing: opcode, `u16 LE` length, `u32 LE` offset, padding byte, and still-XORed data. No decryption or execution permit follows. Zero-offset, malformed-length, and out-of-bounds packets fail before mutation. Even an in-range packet is refused for execution. |
| A6 compare | Parses strict success/failure reply fixtures, including raw `f5 00`. Separate hypothetical comparisons use supplied image bytes. Neither operation reads unknown device bytes. |
| All factory execution | Always rejected, including A2/A4/A5/A8, data/OTP operations, configuration resets, unlock, and RAM-helper claims. |
| Memory state | Private synthetic bytes are an oracle, not host knowledge or a device backup. Main flash, System FLASH, manufacturer data, options, ESIG, and external authentication have separate modeled state. |
| Faults and malformed responses | Invalid requests block the model and revoke the active operation permit. Modeled interruption preserves uncertainty. No physical rollback, atomicity, durable ACK, or safe retry is established. |

Sources: [implemented parsers](../keyboards/ck5200/recovery_lab/lab.py#L48-L110), [factory execution rejection](../keyboards/ck5200/recovery_lab/lab.py#L289-L294), [pinned protocol source](https://github.com/ch32-rs/wchisp/blob/553327eeeea9fdc30414f1e5c74d1ff14b228a26/src/protocol.rs#L145-L329), and [research model proposal](../research_notes/CK5200%20USB%20recovery%20feasibility/isp-preservation.md#exact-offline-python-lab-assumptions). The proposal is broader than the implementation. In particular, its A3/A7 discussion is not implemented functionality. The eight-byte ISP UID field is not established as a byte-for-byte copy of the RM's 96-bit ESIG UID.

### The hypothetical erase contract is stronger than CH32 page erase

**`tail_preserving_erase` must be true before any modeled write.** It promises that all 40 excluded tail bytes remain unchanged, even during interruption. The model's last erase changes only the application bytes in that page. **This is not known CH32 page erase, not a discovered factory wire opcode, and not R3 evidence.** The test suite includes ordinary whole-page erase only as a counterexample that destroys the tail. Reading and later restoring those bytes would not meet the strict requirement during a failure. ([Contract and gates](../keyboards/ck5200/recovery_lab/lab.py#L119-L178), [hypothetical erase](../keyboards/ck5200/recovery_lab/lab.py#L214-L224), [counterexample](../keyboards/ck5200/recovery_lab/test_lab.py#L139-L147))

The model also requires bounded erase/program, documented compare and tail-read capabilities, and independent entry after power loss. Every capability defaults false. Unknown or protected RDPR, unknown write protection, missing capabilities, or unreadable tail blocks preparation before mutation. Each operation requires the exact next stage/page and the validated image bytes. Failure revokes the permit. These checks constrain the model, not an actual loader. ([Preparation and operation guards](../keyboards/ck5200/recovery_lab/lab.py#L166-L263))

### Host restart does not demonstrate a durable journal

The frozen in-memory `Journal` binds the image, original tail, identity, and hypothetical contract. `host_restart()` retains simulated device memory but removes host transaction state, including the accessible journal and active permit. The restarted model refuses restoration until the original journal object is supplied. Simulator-only provenance checks that object's identity. **No durable host journal, disk persistence, crash-safe serialization, or production restart recovery is implemented.** The original journal retained by the test is an oracle, not evidence that an iPhone or adapter survives a real power cut. ([Restart fixture](../keyboards/ck5200/recovery_lab/lab.py#L275-L287), [restart test](../keyboards/ck5200/recovery_lab/test_lab.py#L172-L188))

### Ten tests cover concrete model boundaries

The verified command was `python3 -B -m unittest discover -s keyboards/ck5200/recovery_lab -v`. It reported **10 tests, OK**. These test methods include table-driven cases, not ten hardware trials.

| Test method | Verified scope |
| --- | --- |
| `test_stock_and_bad_images` | Exact V122 length/hash accepted. Empty, truncated, extended, and altered images rejected before mutation. |
| `test_identity_and_independent_boot` | Named trace identities accepted. Wrong/unpinned profiles and missing independent-entry assumptions blocked. |
| `test_protocol_fixtures_and_rejected_execution` | A4 count, actual A5 framing/range rejection, strict A6 replies, and denial of all factory execution. |
| `test_gates_and_tail_unknown_or_unreadable` | Protection, each missing contract capability, and inaccessible tail block before mutation. |
| `test_boundaries_alignment_and_monotonic_bits` | Wrong addresses, data, alignment, and attempts to program zero bits to one fail. |
| `test_exact_transaction_and_stale_operations` | Wrong-order, replayed, stale, and unauthorized operations fail without mutation and revoke the permit. |
| `test_strict_tail_and_ordinary_erase_counterexample` | Ordinary final-page erase changes the tail. Missing stronger erase contract blocks restoration. |
| `test_success_and_oracle_failures` | Hypothetical image/region match succeeds. Deliberate image, configuration-page, and System FLASH corruption fails the oracle. |
| `test_host_restart_requires_original_journal` | Restart loses transaction state. Missing or altered journal is refused. Only the original in-memory fixture permits modeled retry. |
| `test_partial_power_loss_lost_transfer_and_retry` | Erase, program, compare, and lost-transfer faults at first/last pages preserve excluded bytes in the model. Retry requires the explicit survival assumption. |

The CLI checks ran `python3 -B keyboards/ck5200/recovery_lab/lab.py` and the same command with `--hypothetical`. The captured default output reported `BLOCKED: Independent factory entry/intact loader unproven; app cannot help.` The hypothetical output stated `no OEM execution proof`, `No durable journal exists.`, and `Real CK5200 restoration remains BLOCKED.` Both matched their expected exit codes. The model checks excluded bytes at every modeled interruption checkpoint. It does not simulate arbitrary physical fault timing, electrical behavior, the OEM installer, iPhone/BLE control, or CPU boot.

**Both independent agent reviews are closed for the conceptual research and offline-model scope.** The coordinator supplied the final results, recorded in the [durable review summary](../research_notes/CK5200%20USB%20recovery%20feasibility/review.md). This is a summary of agent findings, not a full transcript or new vendor evidence. The final ISP/lab review closed all four initial blockers: operation permits, strict tail preservation, host-restart state, and actual A5 packet handling. It reported no actionable blockers remaining.

The ISP/lab reviewer independently ran the 10 existing tests, direct forbidden-mutation checks, 11 A5 cases, and 292 fault/host-restart/retry combinations. The latter cover four stages across all 73 application pages. Strict 40-byte tail preservation passed, and missing, altered, or other-instance journals were refused. Default exit 2 and hypothetical exit 0 matched expectations. These are reviewer checks, not additional committed test methods or physical trials.

The electrical reviewer independently inspected photos 09, 22, and 24, official WCH document hashes, and the conditional package pin map. The review checked the unknown sample window, mutable BootAsUser region, and reference pull-up to the MCU's own supply. It recomputed numeric bounds and validated the candidate parts and USB-C/host documentation. It reported no P1/P2 blockers for the conceptual scope. Neither review proves a safe CH32 tail-preserving operation, durable journal, OEM boot, or hardware preservation.

## 7. The next step is an evidence packet, not an adapter

### The shortest safe experiment today is an offline contradiction check

**Recommendation, narrowed on 2026-10-17:** resolve R3-6 selective erase/program effects and the unknown tail before proposing an adapter. WCH must define loader effects, and Clicks must define storage and installer behavior. The other entry and electrical gates remain recorded for later review. Repeating normal stock diagnostics would add no missing boot or erase evidence.

| Experiment boundary | Concrete plan |
| --- | --- |
| Hardware today | Existing computer only. No keyboard, phone, USB fixture, debug probe, or external supply participates. |
| Inputs | Pinned WCH PDFs/EVT, pinned `wchisp`, official ISP archive source/header, existing photos, and archived image metadata. Seek existing CK-5200 hardware `1.2.0` schematic/loader records. |
| Offline observation | Record whether each required net, timing bound, loader operation, and memory effect has direct model-matched evidence. Use the reviewed lab to reject unsupported assumptions. Both scoped agent reviews are closed. |
| Expected result from present evidence | PC17 power-up rule remains proven at chip level. Keyboard electrical window remains blocked. Zero-based client remains rejected. No safe app erase or tail backup appears. |
| Falsifying result | A credible model-matched record shows an app-dependent switch, absent loader, protected-only destructive restore, or no safe bias/sample overlap. That defeats this route without touching the unit. |
| Stop/abort | Stop when evidence requires physical probing, unknown commands, downloaded-library execution, firmware installation, unlock, purchase, or sending a vendor message. Keep the corresponding gate blocked. |
| Risk | Source/model mismatch or simulation overclaim. Control it with exact pins, source hashes, and explicit unknowns. There is no target hardware risk because no target participates. |

### Vendor inquiry draft, UNSENT

The [2026-10-17 focused technical inquiry](../research_notes/CK5200%20USB%20recovery%20feasibility/vendor-inquiry.md) supersedes this broad draft as the next message to consider.
Sending either draft requires a separate decision. Both request existing evidence, not a live experiment on this keyboard.

> Subject: CK-5200 hardware 1.2.0, independent recovery and preservation evidence
>
> I have one working CK-5200 for iPhone 15 Pro Max, hardware 1.2.0, stock firmware 1.2.2. I am researching recovery before any custom installation. I will not test undocumented commands, open the keyboard, change protection, or risk this unit to discover a recovery path.
>
> Does this revision retain the CH32X035 System FLASH factory loader? Please identify the MCU variant, loader revision, initial BOOT configuration, and applicable RDPR/WRPR policy. Is there a supported recovery method when the application at offset 0x2000 cannot execute?
>
> Can the inner male USB-C plug reach PC17/D+ and PC16/D− before application startup? Please provide the relevant schematic/netlist, flex mapping, intermediate components, switch defaults, and CC termination at zero MCU VDD. Please include the VBUS-to-VDD power path, discharge/ramp bounds, alternate-source isolation, and supported PC17 threshold/setup/hold/release requirements.
>
> Can recovery restore the exact V122 application at 0x2000–0x68D7 without erasing or programming 0x0000–0x1FFF, other unknown main flash, serial/config pages 0xF600/0xF700, options, System FLASH, or authentication state? The rounded final page includes 40 unknown bytes at 0x68D8–0x68FF. How are those bytes preserved?
>
> Please specify the exact erase/program/verify operations, offsets, minimum erase size, final-buffer effects, protection restrictions, status meanings, and power-loss/retry guarantees. Does any supported USB factory operation load and execute RAM code without a flash write or protection change? Please distinguish device-side compare from actual code readback.
>
> Can an iPhone operate the supported method without the running keyboard application's ExternalAccessory session, either directly or through an external BLE-controlled USB host? Existing engineering documentation or vendor fixture evidence would help establish feasibility before any experiment. No message or test is authorized by this draft.

### A later generic fixture would test the chip and adapter, not the keyboard

**Future option only, requiring separate authorization for hardware, construction, and each operation.** Use a generic CH32X035C8T6 evaluation fixture matching WCH's pinned C8T6 schematic, not a second Clicks keyboard. Use the module and exact candidate parts in §2 only after the missing interlock/sense/discharge design passes review. The fixture must expose its own VDD, PC17, PC16, and ground. Required equipment includes a current-limited regulated supply, oscilloscope with suitable probes, USB capture capability, and an iPhone for BLE integration. No equipment availability, purchase, build, or hookup is assumed. Exact measurement models and ranges must follow the closed electrical limits, not precede them. ([WCH C8T6 reference sheet p.4](https://raw.githubusercontent.com/openwch/ch32x035/c6253942b29ff2a3f5e2829182a874afffa45881/EVT/PUB/CH32X035SCH.pdf))

| Future case | Expected observation | Failure/abort and residual risk |
| --- | --- | --- |
| Bounded supply and BOOT entry | Measured VDD/PC17 remain inside qualified limits. Bias precedes the proven sample and fully releases before host drive. Generic loader enumerates independently of test application execution. | Abort on overvoltage, uncertain sample window, current fault, unexpected drive, or back-power. Generic enumeration does not prove CK-5200 continuity. |
| Delayed/absent target rail or adapter brownout | Hardware holds bias and host disconnected, disables VBUS, and latches the fault. No automatic retries occur. | Abort on retained bias, cross-source current, rail lifting, or unbounded fault response. The fixture or adapter could be damaged if the design is wrong. |
| BLE loss or app suspension | Local watchdog reaches the defined state without phone timing. Logs preserve uncertainty. | Abort on indefinite bias or automatic destructive cleanup. This tests control independence, not keyboard recovery. |
| Selective restore and power cuts, only if separately authorized later | A disposable generic memory fixture demonstrates the documented operation footprint and recovery service after each interruption. Excluded regions compare against known fixture contents. | Do not start without a proven selective primitive. Any outside-range change, option mutation, or lost entry defeats the method. Generic fixture bytes cannot prove this keyboard's unknown bytes. |

The first fixture work would concern electrical bounds and generic identification, not exploratory programming. Its write/power-cut row is a separate future phase, conditional on a vendor-supported operation. No success there would close R3-2 or R3-8 for the CK-5200 without model-matched board evidence.

### Evidence inventory and reproducibility

The three research notes contain the complete source ledgers and bounded inspection results: [boot entry](../research_notes/CK5200%20USB%20recovery%20feasibility/boot-entry.md), [electrical design](../research_notes/CK5200%20USB%20recovery%20feasibility/electrical.md), and [ISP preservation](../research_notes/CK5200%20USB%20recovery%20feasibility/isp-preservation.md). Their direct source pins are preserved here. PDF locations are one-based physical pages unless a printed page is explicitly named. Page offsets vary in the English RM, so printed and physical references must not be interchanged globally.

| Source | Version/pin and scope | Integrity identifier |
| --- | --- | --- |
| [WCH English datasheet](https://www.wch-ic.com/download/file?id=381) | V2.3, 47 pages. Pinout pp.13,17–21, electrical pp.25–29 | SHA-256 `1714c07d9448bc87ec667a602eba3e4c784a6ca7ca25ef125445df0ee4cc755d` |
| [WCH English RM](https://www.wch-ic.com/download/file?id=382) | V1.9, 261 pages. Memory pp.5–6, reset p.13, flash pp.238–250 | SHA-256 `c5dc49eb6a0086857bc120cf4c1646fa8530006fe5a21e7bbb632a075daf036a` |
| [Chinese datasheet](https://file.wch.cn/download/file?id=443) / [RM](https://file.wch.cn/download/file?id=445) | V2.3, 41 pages / V1.9, 246 pages. Cross-language checks in boot note | SHA-256 `a3f2a0f66cfc75368ed5d71b44cfba0d18170efe1a3295bc90a8b647a4913aba` / `c7e301eac4790ca1ba112f946bf057139ec7f36be28e142cafc1c286bcc9daa4` |
| [Official EVT](https://github.com/openwch/ch32x035/tree/c6253942b29ff2a3f5e2829182a874afffa45881) | Commit `c6253942b29ff2a3f5e2829182a874afffa45881`. Guide V1.3, 12 pages. Schematic 7 pages. BootAsUser guide V1.2, 4 pages | Guide `0b4d8179e08aec85a9582b5728fc6c0129b754e7c24a27b604f3ce049a33c5b8`. Schematic `06edb61f5546e9c0dcff9c4495f21cc549e50c3018fde387882965d31bd502c8`. BootAsUser `74a61aea48c57833a01cbb837339ccd39aa93ceab3df793ba7b39a16c86eb3a0` |
| [Pinned WCH mirror](https://github.com/ch32-riscv-ug/CH32X035/tree/74c247d6d518052316b35b68ddc82db2a70afc52) | English DS/RM bytes match direct WCH downloads in the boot study | Commit `74c247d6d518052316b35b68ddc82db2a70afc52` |
| [`wchisp`](https://github.com/ch32-rs/wchisp/tree/553327eeeea9fdc30414f1e5c74d1ff14b228a26) | Actual source and X035 YAML, not README-only evidence | Commit `553327eeeea9fdc30414f1e5c74d1ff14b228a26` |
| [Official ISP ZIP](https://www.wch-ic.com/download/file?id=365) | 4,143,391 bytes. Page metadata says 3.6, wrapper says V3.70, library filenames say 4.0.0. Use hash, not one invented package version | SHA-256 `2da0b385bc789ab9cd468dd6dc29ec117a98dd00ad92e5ad5c9fb5b66868c4bd` |
| [Type-C R2.4, October 2024](https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/196/USB-Type_2D00_C-Spec-R2.4-_2D00_-October-2024.pdf) | TI-hosted USB specification. §§4.2–4.5, tables 4-27/28, timing tables 4-32/33 | SHA-256 `f5407e6120db1e28137e7fee60f3125f4284e2252b1240d271ce6e40c0ba3121` |
| [USB 2.0 archive](https://www.usb.org/sites/default/files/usb_20_20250603.zip) | `usb_20_20250603.zip`, base §7.1.5/7.1.7.5 and resistor ECN pp.1–4 | Archive `5fe9c53c04033818af396e8852b3acbca5c3a76ba92fab549fd81cd0ea7b3692`. ECN `7d54e297fd8b79706a37912a899d20dca08d01861b940f4ebe2c11498d492e51` |
| [Electrical component ledger](../research_notes/CK5200%20USB%20recovery%20feasibility/electrical.md#6-which-sources-and-exact-document-versions-support-this-study) | ESP datasheet v2.2, TS3USB221A SCDS277C, TUSB320LAI SLLSEQ8D, TPS25947 SLVSFC9C, LDO/ESD/module PDFs | Exact retrieved PDF hashes in that ledger. Host-library `latest` URL remains mutable. |
| [Apple Core Bluetooth JSON](https://developer.apple.com/tutorials/data/documentation/corebluetooth.json) / [usage-key JSON](https://developer.apple.com/tutorials/data/documentation/bundleresources/information-property-list/nsbluetoothalwaysusagedescription.json) | Retrieved 2026-10-16. iOS framework and usage-key requirements | Current documentation response, not a pinned SDK or runtime test |

The notes report PDF/source hashes, source inspection, photo integrity checks, and arithmetic. They report no circuit-simulator or hardware result. This report adds the locally rerun Python lab tests and both captured CLI outcomes described in §6. Both scoped independent agent reviews are closed, with their reported results retained in [review.md](../research_notes/CK5200%20USB%20recovery%20feasibility/review.md). No firmware, Xcode, device simulator, hardware, or ISP execution is claimed. The existing M1/M2/M3 check results remain historical evidence within their original limits.

Report checks: `python3 scripts/check-readiness.py` and `git diff --check` passed. Local checks found no missing document paths, invalid local heading targets, or trailing whitespace in the five owned documents and added review summary. Python lab checks passed as recorded above and were not repeated for this documentation-only review closure. Firmware builds and device/runtime checks were skipped because this task permits only documentation and offline models.

## Conclusion

**The useful next decision concerns evidence, not adapter parts.** A vendor-supported selective restore mechanism and model-matched electrical record would change the investigation materially. Without them, a working BLE host could reach a loader yet still have no permissible way to repair the application. That is the distinction between plausible entry and acceptable recovery.

Keep the only keyboard on its accepted stock baseline. Continue only the offline evidence and model work. The specific unresolved gates are connector/default-state continuity, VDD/bias timing, retained loader identity, protection state, selective erase/program semantics, all unknown-byte preservation, OEM startup/staging behavior, and interruption recovery. The eight R3 criteria organize that proof. Only [flash-readiness.md](../keyboards/ck5200/docs/flash-readiness.md) owns the resulting authorization decision.
