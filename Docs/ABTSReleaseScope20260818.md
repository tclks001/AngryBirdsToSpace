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
- Shipping hard-disables DeveloperWalk, unrestricted slingshot-stake placement,
  TaskGraph coordinate overlays, debug slingshot spawning, bridge debug drawing,
  and maximum-inventory seeding even when a map or serialized class default
  requests one of those development conveniences.
- The release moon restores the player-validated `1960 cm/s^2` surface gravity
  for `BallisticFlight`. Idle and walking birds receive zero satellite force;
  the primary-planet authority continues to resolve the largest ready
  non-satellite planet and applies its own `980 cm/s^2` radial gravity.

### M7 per-brick runtime P0

The user rejected M7 experiment `e41b6b7` in visible Standalone on August 17,
2026. E2 impact produced an approximately three-second hitch followed by
progressive collapse with floating bricks; an E3 black-bird explosion left
unnaturally standing unsupported bricks. The experiment must not be integrated.
Its runtime log recorded 3,247 complete support-closure derivations and 2,718
`FixedSixSupportClosureNoPublicationSlots` damage drops in one session. A single
building damage epoch must coalesce contacts, derive its support closure once,
and publish every unsupported brick into immediate independent visible motion.
No unsupported brick may remain frozen merely because a Chaos publication slot
is unavailable, and the rejected coarse rigid-cluster fallback remains outside
the release candidate.

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
- Building-to-road visual clearance changes. The frozen sites still consume the
  launch-range contract, but the enlarged production building envelopes make
  several facades read closer to the main road than the earlier corridor
  previews. August 18 keeps the current Placement/Layout identities unchanged;
  a later pass must constrain building OBB-to-road-edge clearance instead of
  translating sites ad hoc.

## RC9 packaged cinematic acceptance

### User-accepted RC9.2 baseline

The user completed an in-game acceptance pass on
`candidate-c9037ca-20260817-Development` on August 17, 2026. The following
items are accepted and are removed from the open investigation queue:

- the loading progress page remains present until the correct entry UI, with
  no erroneous intermediate frame;
- the opening camera and bird animation play correctly;
- the stylized sky, star field, and low-poly clouds render correctly;
- all four finale controls respond to pointer input.

Later candidates require only a normal packaged startup/finale regression
smoke for these accepted paths. Do not reopen their root-cause investigations
unless a later package visibly regresses one of them.

### Shipping startup handoff P0

The Shipping foreground is armed before the authoritative M6 startup actor is
guaranteed to be discoverable. The world must therefore remain unpaused from
that first armed frame until the startup authority reports either Ready or
Failed. Pausing based on gate discovery creates a Shipping-only bootstrap
deadlock: the MoviePlayer screen completes, the in-game handoff cover reaches
its 92 percent pre-Ready cap, and the actor needed to open the gate never gets
another world tick. The installed UE build cannot safely enable global Shipping
logging without a log-category ABI mismatch, so Shipping verification uses
project-owned markers, process state, and offscreen visual evidence instead.
The first game-world handoff also preserves the MoviePlayer foreground clock
and uses the same black visual field, title/status palette, flat progress track,
and 30-second progress function. `PostLoadMap` is not `WorldReady`: it leaves
the bar below 100 percent and keeps the status at `GENERATING PLANETARY WORLD`.
Only the authoritative runtime gate may publish `WORLD READY` and 100 percent.
The interactive front end also fails closed before the M6 authority actor is
first discovered, so the blue front page cannot leak between the two technical
owners. The result is one continuous player-facing loading page rather than a
second, differently styled loading pass.

The first visible Shipping world exposed two additional cooked-only fallbacks.
`AABTSM3Planet` and `AABTSM51GameMode` had compile-time Shipping branches that
discarded the production map's authored frozen V3 presentation request and
ordinary-slot snapshot, restoring the short straight compatibility world and
its sparse legacy stakes. Shipping now preserves the authored Candidate 4
frame while continuing to reject command-line preview overrides. In the same
package, M7 building HISM instances fell back to the checker/default material
because the shared toon master lacked the cooked InstancedStaticMesh usage
permutation. The master material now owns that usage and the M7 adapter test
checks it explicitly. Release evidence must therefore show
`MapFreezeV3MainRoute / Candidate=4 / EndpointOrdinal=0`, accepted ordinary
slot snapshots, and no HISM default-material fallback in the cooked player.

The unified handoff was verified by a full UE 5.8 Development Editor build and
the fresh `ABTS.UI` NullRHI suite (4/4). The active candidate also integrates
the M7 fixed-six static-startup acceptance and one-closure-per-building damage
epoch changes. Its full Shipping BuildCookRun completed with ExitCode 0 at
candidate `7e40760`; the archived package is
`G:\\ABTS\\Artifacts\\RC9.3\\candidate-7e40760-20260817-Shipping`. A packaged
Shipping player-path D3D11 offscreen trace reached `AuthorityReady=1`,
`PresentationReady=1`, and `WorldReady=1` roughly two seconds after BeginPlay.
All six buildings were already `Accepted=6`, with `Pending=0`, `Running=0`,
`Rejected=0`, and `WorldFailed=0`; the evidence is
`G:\\ABTS\\Logs\\RC9.3\\StartupTrace-candidate-7e40760-20260817-Shipping-playerpath.log`.
This closes the previously reproduced Shipping-only startup wait in automated
packaged evidence. Visible packaged acceptance and real first-hit per-brick
performance/collapse quality remain required before final release promotion.

- RC9 Development and Shipping both play the opening and post-hit finale
  cinematics. `abts.Debug.SkipCinematics` therefore defaults to `0` for RC9
  Development, and Shipping hard-locks playback even if a skip request is
  injected.
- If RC10 or a later Development iteration is required, its debug default may
  be changed to skip both cinematics for faster gameplay iteration. Shipping
  remains permanently hard-locked to playback.
- The opening must be anchored to the real spawn/party frame and hand off
  without a bird-position jump. The finale must begin from the authoritative
  UFO hit in its real local frame and trigger once. Preview-only substitute
  worlds are not release evidence.
- The opening UFO capture beam may not ship as the Engine BasicShapes cylinder;
  RC9 requires a stylized, collision-free presentation effect tied to the same
  deterministic capture interval.
- Integration production binding is one-shot and fail closed: only a formally
  certified, non-Candidate `PhysicalTargetHit` may start the post-hit timeline.
  Its first proxy frame is copied from the four real bird visuals, the root is
  the authoritative UFO transform, and the real presentation is retired only
  after the replacement Geometry Collection is ready. A rejected binding keeps
  the already-authoritative gameplay `TargetHit` instead of fabricating a
  preview success.

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

## Codex coordination and quota policy

The primary Integration coordinator retains the global release graph, owns
cross-system decisions, reviews every delivered diff, serializes heavy Unreal
work, and is the only writer in the original Integration worktree while an
integration edit is active. Model effort is selected by task risk rather than
being inherited blindly from the coordinator:

- use `gpt-5.6-sol` with high effort for cross-owner contracts, ambiguous root
  causes, merge arbitration, release acceptance, and changes whose failure can
  invalidate several systems;
- use `gpt-5.6-terra` with medium or low effort for bounded implementation,
  focused code review, and test-failure diagnosis with clear ownership;
- use `gpt-5.6-luna` with low effort, or another available low-cost model, for
  log extraction, inventory, mechanical audits, test watching, and other
  narrowly specified high-volume work;
- escalate one tier only when the lower-cost result is incomplete, internally
  inconsistent, or fails its stated evidence contract. Do not keep a frontier
  model on routine polling or transcription work.

The same policy applies to M3, M7, M11, and additional Integration tasks. One
worktree has exactly one source/config/asset/document writer at a time. Other
tasks sharing that checkout must be read-only. If a parallel task must emit an
artifact, it may write only beneath
`Saved/CodexCoordination/<task-id>/` (or the corresponding project-owned path
on `G:\ABTS`) and must not stage, commit, or modify tracked files. Heavy build,
cook, package, D3D, Chaos, and visible acceptance work remains globally
serialized regardless of model tier.
