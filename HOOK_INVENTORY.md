# Hook Inventory — Rack Randomizer

## Plugin metadata

| Field | Value |
|---|---|
| Plugin name | Rack Randomizer |
| Plugin ID | rack-randomizer |
| Release version | 1.0.0 |
| Inventory updated | 2026-11-10 |
| Source revision | v1.0.0 |
| D2R build(s) audited | 3.4.95032 with D2RLoader 1.3.1-beta |
| D2RLoader SDK / Plugin ABI | 0.4.0 / 4 |
| Audit status | Complete |

## Scope and method

Reviewed `src/plugin.cpp`. The DLL uses the SDK mutation service to stage both picker-slot replacements in one transaction. The hook sites match the tested 0.0.1 development build.

## Hook sites

| Site ID | Type | Target | Address / identifier | Operation | Guard / validation | Source reference | Notes |
|---|---|---|---|---|---|---|---|
| H-01 | shared slot | Armor picker dispatch pointer | RVA 0x3E2ACA8 | Substitute a local RNG for calls from the native armor drop routine | Slot predecessor, thunk bytes, native routine bodies, caller return address and offline mode | src/plugin.cpp, `SelectArmor` and `StagePicker` | Uses the original D2RCore selector |
| H-02 | shared slot | Weapon picker dispatch pointer | RVA 0x3E2ACB0 | Substitute a local RNG for calls from the native weapon drop routine | Slot predecessor, thunk bytes, native routine bodies, caller return address and offline mode | src/plugin.cpp, `SelectWeapon` and `StagePicker` | The native metal retry loop remains in control |

## Known collision overlaps

None identified in the current inventory; this is not a compatibility guarantee.

| Other plugin | Shared target | Relationship | Evidence / notes |
|---|---|---|---|
| None | N/A | N/A | The Drop Logger hooks the upstream dispatcher. Random Treasure Drop hooks the treasure-class routine. |

## Audit notes

Both native rack paths, the selector seed contracts, and the offline mode value were inspected live. The 0.0.1 offline smoke test confirmed that both racks used private RNG and generated one item each. Other game builds and CrossOver remain untested.
