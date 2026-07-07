# Property Matrix Data Editing — Weapon Balance Stats

Weapon balance stats (`UFPSRWeaponDataAsset::BaseStats`, `FFPSRWeaponStatBlock`) are edited with UE's built-in
**Property Matrix**, not a custom tool:

1. In the Content Browser, select every weapon `DataAsset` you want to tune together (multi-select).
2. Right-click → **Edit Selection in Property Matrix**.
3. Expand `BaseStats` and pin the columns you're tuning (`Damage`, `FireRate`, `MagSize`, `SpreadDegrees`,
   `RecoilVertical`, etc.) so every selected weapon's value for that stat is visible and editable in one
   spreadsheet-like grid.

## Why no custom weapon matrix was built

`FFPSRWeaponStatBlock` is a flat struct of plain scalars (`float` / `int32` / `bool` / a few enums) with no
`Instanced` polymorphism and no cross-asset membership semantics. The engine's Property Matrix already reaches
every one of those fields for free across an arbitrary multi-selection — there's nothing here a hand-rolled grid
would add. Building one would duplicate engine functionality for no gain (over-design gate, P1).

## What the FPSR Data Editor covers instead (Tools > FPSR > Data Editor...)

The custom **FPSR Data Editor** tool exists only for the things the Property Matrix genuinely cannot do:

- **Pool / loadout / schedule wiring membership** — adding or removing a card from a `UFPSRCardPoolDataAsset`'s
  `Cards` / `WeaponUnlockCards`, a weapon's `WeaponCards` / `UnlockableFeatures`, a `UFPSRLoadoutPoolDataAsset`'s
  `SelectableWeapons`, or a `UFPSRRunScheduleDataAsset` mission window's `MissionPool` — with a routing preflight
  so a card can't silently land in a route its effects don't support.
- **Orphan fixing** — surfacing cards / weapons / missions unreachable from any anchor (reusing the P0
  `FFPSRAnchoredValidationService`) with a guided "wire this in" affordance.
- **Card per-rarity magnitude** — editing `FFPSRCardRarityTier::Magnitude` values buried inside a card's
  `Instanced` `UFPSRCardEffect` subobjects. The Property Matrix does not descend into `Instanced` array elements'
  own array properties, so this needs the dedicated magnitude grid.

Everything else about editing a *selected* asset's properties (including the membership arrays themselves, once
you've navigated to the right asset) is handled by the engine's own `IDetailsView` inside the Data Editor tool —
no hand-rolled property editing beyond the three widgets above.
