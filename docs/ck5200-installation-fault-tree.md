# CK-5200 physical rejection fault tree

Observed fixture: both capability requests returned **`080294ff00000000`**.
Both requests contain opcode `0x94`. The intended extension returns `CKX1`.
This tree separates observed rejection from the unknown upstream physical cause.

Evidence keys:

- **P**: preserved physical audit, exact client bytes, all 848 advancing A2
  ACKs, A3 size ACK and A0 ACK. No trusted device readback/reset completion.
- **I**: [decrypted iOS data flow](ck5200-original-ios-investigation.md).
- **V**: [V122 staging/reset/flash contract](ck5200-executable-and-oem-evidence.md#oem-staging-and-reset-contract).
- **E**: [exact executable controls](ck5200-executable-and-oem-evidence.md#original-receive-path-separately-bounded).

Verdicts concern each hypothesis as an explanation of the **physical** failure.
An offline reproduction proves possibility only. No evidence supports ranking
the remaining physical hypotheses.

| Hypothesis and mechanism | Support | Contradiction or limitation | Offline reproduction | Distinguishing observation | Verdict |
| --- | --- | --- | --- | --- | --- |
| Incorrect iOS byte-order/framing: malformed update leaves OEM active | I encoder versus P byte comparison | All 851 requests match. Both capability requests are valid for E. | Encoder/capture comparison and both request fixtures | A differing transmitted frame, absent from P | **Falsified for captured normal framing**, not for unobserved physical flash effects |
| Omitted material iOS-only installation command: copier never activated | I worker finishes after A0. Direct helper cross-references and async return inspected | No second installation command on this path. Runtime-generated/unexamined routes are not absolute negatives. | Reconstructed worker A0 result cases | Verified reachable CK-specific command path beyond the inspected worker | **Falsified on inspected EAF path**, broader absence scoped |
| Raw/padded length incompatibility: copier rejects `0x6A00`, stock remains | I sends raw. P padded. V A1 accepts maximum | A1 acceptance does not prove copier acceptance. E raw/padded paths behave alike only after application load. | Raw/padded startup and receive controls do not reproduce copier | Matching copier length/format rule and actual marker/staging bytes | **Undecidable** |
| Incorrect staging bytes: transfer ACK advances but active image remains stock/damaged | P lacks readback. V ACK does not hash programmed bytes | Captured source bytes/hash are correct. This contradicts client-source corruption, not flash corruption. | Client-source mismatch excluded. No synthetic flash model is substituted. | Trusted staged bytes or device-side hash | **Plausible** |
| Silent flash programming failure: BUSY clears with error, ACK continues | V tests only BUSY, no readback/error propagation | No physical status register/error log retained | Static error-path audit, not physical failure injection | STATR/error evidence and staged-byte readback | **Plausible** |
| Invalid/missing marker: A3 acknowledged, copier sees invalid/no pending image | V A1 invalidates RAM marker. A3 writes stored length without independent flash verification | P A3 size ACK, not marker readback | Contract audit. Marker contents cannot be emulated as evidence of OEM behavior. | Trusted marker contents at reset and copier rule | **Plausible** |
| A0 ACK without completed reset: old stock dispatcher continues | P ACK only. V reset request is not copy | Later typing and FF do not distinguish reset from no reset | Vendor A0 timeout/rejection still reports success in reconstructed model | Completed MCU reset/application-entry observation | **Plausible** |
| Boot-stage validation rejects image: old image retained | Missing copier and no destination readback | No recovered rejection criteria prove this happened | Not reproduced. Inventing validation would be circular. | Actual matching copier code/trace and staging | **Undecidable** |
| Boot-stage copy skipped: pending state not acted on | No copier trace/marker readback | Same P ACK sequence permits successful or skipped copy | Not reproduced without OEM stage | Copier entry/branch trace and destination bytes | **Undecidable** |
| Boot-stage copy interrupted: OEM hook retained or partial image executes | No copy-completion evidence or destination readback | Keyboard typing contradicts complete loss of all application function, not partial replacement | E hook-disabled/redirect controls reproduce FF, not copy interruption itself | Destination bytes, copier progress and reset chronology | **Plausible** |
| Different application image executes: it rejects `0x94` | P FF, E stock reproduces exact bytes | P transfer hash does not identify active image | Unmodified V122, both captured requests | Trusted destination identity/hash | **Plausible** |
| Partial installation preserves OEM dispatcher: `0x2218` remains original | E restored-hook control produces exact FF | No physical hook bytes are readable in retained evidence | Command hook restored to original instruction | Destination hook and extension-byte readback | **Plausible** |
| Correct image copied but startup fails: no extension command service | Full execution after RCC is unsupported | E executes CRT/init. Normal typing and deterministic FF argue against a total nonrunning application, unless fallback/different code executes. | Full startup stops at exact hardware-model boundary, not a proven guest failure | Actual startup trace and active image identity | **Undecidable**, total application death alone does not explain FF |
| Extension initialization failure: dispatch state remains invalid or OEM route survives | Initialization affects BSS/state. P cannot observe it | E skipped-init guard stops, does not return FF. Exact-image init succeeds to SystemInit boundary. | Skipped initializer and corrupted startup SP | Extension BSS/init/hook evidence after actual startup | **Undecidable**, not reproduced as FF by these controls |
| Extension code erased/corrupt: changed execution reaches OEM or traps | P has no destination readback | Chosen erased-code mutant traps, not FF. Corruption is not one unique outcome. | Erased extension and intentional OEM redirect | Exact destination code bytes | **Plausible** for selected corruption, broad corruption remains undecidable |
| Missing/corrupt command hook: `0x94` reaches OEM unknown handler | E disabled hook reproduces both exact replies | Correct source image has hook. P cannot show installed hook. | `0x2218` original instruction | Physical hook bytes or executed branch target | **Plausible** |
| Alternate dispatch bypasses hook: request reaches another OEM dispatcher | P FF does not expose executed PC | Original authenticated adapter reaches `0x2208`/hook in E. The null-callback control returns no reply, not FF. | Original receive route, callback-cleared control | Actual physical callback/dispatch trace | **Undecidable**. The tested null-callback mechanism does not match. |
| OEM unknown-command path receives `0x94` | E stock, hook-disabled and redirected cases return exact fixture | Equal bytes do not uniquely identify physical instruction addresses | Three deterministic FF controls | Physical dispatch trace or destination readback | **Confirmed as an offline mechanism**, physical attribution remains undecidable |

The only confirmed physical outcome is rejection and failed extension
acceptance. The [minimum evidence](ck5200-installation-feasibility-verdict.md#minimum-evidence-that-could-change-this-decision)
must distinguish causes and independently close recovery before any GO.
