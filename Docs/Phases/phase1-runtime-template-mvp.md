# Phase 1: Runtime Hardening and Template Library MVP

## Release

- Plugin version: `0.3.0`
- Engine: Unreal Engine 5.3.2
- Demo map: `/Game/LLMNPC/Maps/M_LLMNPC_Test`
- Automation filter: `LLMNPCActionLayer.Phase`

## Delivered

- Component-local post-process installation using a transient skeletal mesh override.
- Automatic restoration of the original mesh and post-process state on `EndPlay`.
- Plugin-owned `ABP_LLMNPC_PostProcess` default asset.
- Versioned `SP_UE5_Manny_v1` skeleton profile with a reference-skeleton signature.
- Published Primary DataAssets for Nod, faithful Wave FK, and procedural Wave.
- JSON authoring sources for all three templates.
- Runtime template library and skeleton-independent public action IDs.
- Immutable template compiler with constrained amplitude, speed, duration, style, and target modifiers.
- Runtime-model, published-template, and internal-debug validation trust levels.
- Internal-only direct right-arm FK controls.
- Finite-number, key, timing, target, anchor, type, track, and size validation.
- Concurrent channel scheduling with priority, interruption, queue timeout, and template cooldown metadata.
- Existing `TestNod` and `TestWave` Blueprint entry points migrated to Published Templates.
- Legacy hardcoded Nod and Wave plan builders removed.
- Asset Manager and cook rules supplied by plugin `Config/Game.ini`.

## Published Assets

| Template ID | Public action ID | Runtime model candidate | Purpose |
| --- | --- | --- | --- |
| `gesture.nod.manny.v1` | `gesture.nod` | Yes | Neutral procedural nod |
| `gesture.wave.right.manny.fk.v1` | `gesture.wave.right` | No | Faithful reviewed FK baseline |
| `gesture.wave.right.manny.procedural.v1` | `gesture.wave.right` | Yes | Semantic target-independent wave |

## Verification

- UE 5.3.2 editor target build: passed.
- Phase 0 and Phase 1 automation: 11/11 passed.
- Historical Nod and faithful Wave sampler snapshots: unchanged.
- Runtime test-map startup: passed.
- Runtime template scan: 3 templates and 1 skeleton profile, 0 errors.
- Component post-process installation in a game world: passed.
- Windows cook: passed with 0 errors.
- Cooked plugin content contains the default AnimBP, all three templates, and the Manny profile.

## Runtime Boundaries

- Runtime-model validation cannot invoke direct FK arm or hand controls.
- The model-facing public action index contains only Published templates with runtime selection enabled.
- Draft or rejected templates are not indexed.
- Exact skeleton template IDs remain available to trusted game/debug code.
- Target-bearing tracks fail before queueing when the target is missing or invalid.
- The shared source `USkeletalMesh` asset is never modified.

## Remaining Manual Check

Open the demo map in the editor and visually compare:

1. `TestNod` against the accepted Phase 0 nod.
2. `TestWave` against the accepted faithful FK wave.
3. `SubmitPublishedTemplate("gesture.wave.right", ...)` against the procedural wave candidate.
4. Nod and Wave running together to confirm independent head and arm channels.

The first two preserve the accepted sampler data automatically. The procedural wave still requires human visual approval before its motion quality is considered final.
