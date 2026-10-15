# iPhone access to CH32X035 factory USB ISP

Checked: 2026-10-14. Scope: Apple's documented public APIs for an ordinary
iPhone app on standard iOS. ISP means in-system programming.

## Conclusion

**[INFERENCE] No supported public transport was found for an ordinary iPhone
app to access the stated factory ISP device directly.** External Accessory
requires an MFi accessory connection. USBDriverKit supports macOS and
M-series iPads, not iPhone in Apple's documented hardware support.
IOUSBHost lists macOS and Mac Catalyst, not iOS. [1–6]

This conclusion assumes the factory ISP exposes vendor-specific USB bulk
endpoints without iAP2/MFi, as specified in the research question. This
investigation did not independently verify that device description.

**[CORRECTION] "Recovery is Mac-only" is too broad.** Apple's documentation
establishes a USB driver route on supported iPads as well. It does not prove
that a CK-5200 recovery implementation exists there. These Apple sources
also cannot exclude other host operating systems or special vendor solutions.

## Primary-source evidence

| Status | Evidence | Meaning for this question |
| --- | --- | --- |
| VERIFIED | External Accessory says: "Use the External Accessory framework to set up and manage a connection to an MFi accessory your iOS app supports." The accessory manufacturer determines which apps can communicate. [1] | This is an accessory-protocol API, not documented access to arbitrary USB endpoints. |
| VERIFIED | `EASession.init(accessory:forProtocol:)` requires an `EAAccessory` and a protocol the accessory understands. [2] | An app needs an available accessory session. Knowing the ISP endpoint addresses does not establish such a session. |
| VERIFIED | USBDriverKit explicitly states: "USBDriverKit is available on macOS for Intel and Apple Silicon devices, and on iPadOS for devices with an M-series chip." It provides device configurations, interfaces, and endpoints. [3] | Apple documents raw USB driver support on those hosts. It does not list iPhone. |
| VERIFIED | DriverKit describes device drivers for macOS and iPadOS. Its hardware note again restricts iPadOS support to M-series devices. [4] | A USB-C connector alone does not establish DriverKit support. |
| VERIFIED | "Creating drivers for iPadOS" documents support from iPadOS 16, including USBDriverKit. Drivers require user approval. App-to-driver communication requires the relevant entitlement. [5] | Supported iPads have a documented driver route. This is not a general iPhone app permission. |
| VERIFIED | IOUSBHost supports custom and non-class-compliant USB devices within apps. Its published availability lists macOS 10.15+ and Mac Catalyst 14.0+. [6] | This separate USB host framework does not document an iPhone route. |

**Documentation caveat:** DriverKit's current metadata includes an "iOS 16.0"
availability entry. Its explicit hardware note specifies M-series iPads. [4]
Conversely, the USB transport entitlement page lists only macOS, although
the USBDriverKit overview and iPadOS guide explicitly support iPadOS. [3,5,7]
Do not use either availability badge alone to infer hardware support.

## Recovery implications and unknowns

- **[INFERENCE]** Under the stated no-iAP2/MFi assumption, External Accessory
  cannot supply the missing raw USB transport for factory ISP. This is not
  a claim that every USB accessory needs MFi.
- **[INFERENCE]** If factory ISP enumerates independently, the keyboard
  application need not run for a capable USB host to communicate with it.
  That does not give an ordinary iPhone app the required public API.
- **[UNVERIFIED]** Whether this CK-5200 can enter and expose factory ISP
  without its application remains a separate board/boot question.
- **[UNVERIFIED]** A separate Clicks installer could expose another protocol.
  This investigation does not establish its behavior or iPhone reachability.
- **[UNVERIFIED]** Private entitlements, Apple-internal software, and
  vendor-specific recovery arrangements remain unknown. Public API limits
  do not prove that every privileged solution is impossible.
- **[UNVERIFIED]** No CK-5200 ISP driver or recovery flow was demonstrated
  on an M-series iPad. API support alone does not prove successful recovery.

## Sources and verification limits

### API and entitlement follow-up, 2026-10-15

Apple's documentation JSON was checked again for the actual app-facing permissions:

- `EASession` and [`UISupportedExternalAccessoryProtocols`](https://developer.apple.com/documentation/bundleresources/information-property-list/uisupportedexternalaccessoryprotocols)
  list iOS 3.0 availability. The latter is an `Info.plist` declaration, not a raw-USB entitlement.
  It cannot create a protocol that the accessory does not expose.
- [`com.apple.developer.driverkit.communicates-with-drivers`](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.driverkit.communicates-with-drivers)
  permits opening driver user clients. Its metadata lists iOS/iPadOS 16.0.
  USBDriverKit's explicit hardware note still limits iPadOS support to M-series iPads. The badge does not establish iPhone support.
- [`com.apple.developer.driverkit.userclient-access`](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.driverkit.userclient-access)
  lists macOS 10.15 and directs iPadOS apps to the entitlement above.
- `com.apple.developer.driverkit.transport.usb` identifies supported USB devices for a driver.
  It does not supply a general iPhone USB API. Its metadata lists macOS 10.15, subject to the documentation caveat above.

The original installer would need its own supported transport while the application is absent.
The stock application's accepted ExternalAccessory session proves only the running-stock path.
No original-installer descriptor, authentication exchange, protocol advertisement, or independent iPhone session is available as evidence.
Therefore **R3 remains UNMET**, whether that missing service does not exist or merely remains undiscovered.

### Original source list

1. [Apple: External Accessory](https://developer.apple.com/documentation/externalaccessory)
2. [Apple: EASession initializer](https://developer.apple.com/documentation/externalaccessory/easession/init(accessory:forprotocol:))
3. [Apple: USBDriverKit](https://developer.apple.com/documentation/usbdriverkit)
4. [Apple: DriverKit](https://developer.apple.com/documentation/driverkit), including its [documentation JSON](https://developer.apple.com/tutorials/data/documentation/driverkit.json).
5. [Apple: Creating drivers for iPadOS](https://developer.apple.com/documentation/driverkit/creating-drivers-for-ipados)
6. [Apple: IOUSBHost](https://developer.apple.com/documentation/iousbhost), including its [availability metadata](https://developer.apple.com/tutorials/data/documentation/iousbhost.json).
7. [Apple: USB transport entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.driverkit.transport.usb)

The documentation content was read through Apple's public documentation JSON.
Apple's [WWDC22 presentation](https://developer.apple.com/videos/play/wwdc2022/110373/)
also confirms the iPad driver route. Two Apple forum pages returned security
verification pages, so no forum reply supports this conclusion.

This was documentation research only. No device operations, settings changes,
code execution from downloads, firmware writes, resets, ISP commands,
protection changes, or SDI investigation occurred. Hardware and runtime
checks were not performed.
