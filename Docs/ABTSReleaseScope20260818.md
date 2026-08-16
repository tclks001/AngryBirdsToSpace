# ABTS 2026-08-18 Release Scope

> Owner: Integration worktree
> Status: release-scope freeze for the August 18 build
> Production map: `/Game/Maps/L_ABTS_M11`

## Release rule

The August 18 build does not ship a formal building weak-point mechanic. The
production TaskGraph building route already publishes `WeakPoints=0`; legacy
M7.3-B/B2 and DAG3/DAG4 weak-point research remains in source control as
historical evidence, but it is not a release gate and must not be re-enabled by
the renderer, guidance system, damage tuning, or packaging flow.

The player wins the E1 encounter by attacking the real E1 building and causing
its structure to collapse. The player is not required to hit an exact Crystal,
device, or specially highlighted brick.

## Required for the release

- All six frozen V3 buildings register at their published sites.
- E1 is grounded and survives static startup without an immediate Chaos fall.
- A hit on any real E1 descriptor brick can enter the normal damage and Chaos
  collapse lifecycle.
- Accumulated damage, bird-by-material damage, module-to-module chain impact,
  promotion from static HISM to a dynamic module, and building collapse remain
  functional.
- The E1 Crystal may be destroyed as a consequence of the building collapse,
  but it is not an exact-hit target or a gameplay weak point.
- M7 stylized rendering covers the four building material families: Wood,
  Stone, Steel, and Glass. Crystal uses the M7Glass visual family.
- Static HISM and promoted dynamic modules use the same reversible material
  binding; Style Off restores the original slot exactly.
- Building semantic rendering publishes only `BuildingBody` for this release.
- Primary-planet locomotion authority, satellite gameplay gravity isolation,
  startup loading, `WorldReady`, and packaged Development/Shipping behavior
  remain mandatory core gates.
- The release moon restores the player-validated `1960 cm/s^2` surface gravity
  for `BallisticFlight`. Idle and walking birds receive zero satellite force;
  the primary-planet authority continues to resolve the largest ready
  non-satellite planet and applies its own `980 cm/s^2` radial gravity.

## Explicitly deferred

- Unique or authored building weak points.
- `BuildingWeakPoint` runtime publication, Custom Stencil, outline, marker, or
  tutorial guidance. The shared enum value remains reserved for compatibility.
- Weak-point-only materials, glow, textures, destruction transitions, and GPU
  captures.
- Crystal-only aiming, numerical trajectory certification, or exact Crystal
  first-hit requirements.
- Explosive barrel or spring-piston weak-point presentation. Devices that exist
  in a building are rendered as ordinary building bodies for this release.
- Weak-point-specific damage multipliers or DamageLab controls.
- Legacy `1 Weak + 3 Ordinary` rollout matrices and the M73B/B2/DAG3/DAG4
  weak-point suites as release-signoff tests, unless a changed file directly
  requires one of those suites as a regression check.

## Release validation filters

Use focused release filters instead of the broad historical `ABTS.M7` prefix:

- primary-planet locomotion and satellite gravity policy;
- world-generation contracts and frozen V3 identity;
- fixed-six registration, E1 exact building-union binding, damage lifecycle,
  deferred Chaos activation, and settlement/`WorldReady`;
- M7 stylized adapter, material loading, static/dynamic slot binding, and Style
  Off restoration;
- M11 production-map fresh process and packaged startup flow.

ForceUnity remains required after integrating C++ changes. NullRHI proves data
and lifecycle gates, not visible material quality or movement feel. The final
release still needs packaged hand-feel testing and one visible production-map
acceptance pass by the user.
