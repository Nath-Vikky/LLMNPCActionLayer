# Phase 6 Navigation And Compound Behavior MVP

Version: `0.7.5`

This slice adds the first runtime-owned compound behavior path:

```text
move_to registered target
  -> AI MoveTo
  -> face target
  -> optional Published template
```

## Model boundary

The model response remains `llmnpc.model_turn.v1`. The only enabled locomotion
decision is `move_to` with an opaque `target_ref` and requested acceptance
radius. The model cannot provide:

- a world-space destination;
- path points or a navigation query;
- controller or movement-component settings;
- a rotation or facing transform;
- an animation asset path;
- raw controls, bones, or keyframes.

The target must already be available in
`ULLMNPCSceneContextComponent`. UE clamps the requested radius to project
policy before it builds a plan.

## Runtime plan

`FLLMNPCBehaviorPlanValidator` compiles a valid Model Turn into a maximum of
eight engine-owned steps. The current public flow generates:

1. `MoveToTarget`;
2. `FaceTarget`;
3. `PlayTemplate`, when the same turn selected a Published action.

The validator rejects unavailable targets, self-targeting, invalid ordering,
duplicate core steps, invalid timeouts, invalid radii, and missing template
IDs. No Blueprint or provider API accepts arbitrary plan steps in this slice.

## Execution

`ULLMNPCBehaviorCoordinator` owns asynchronous execution. It:

- uses an existing `AAIController`, or optionally asks an unpossessed Pawn to
  spawn its configured default controller;
- rejects a Pawn owned by a non-AI controller;
- submits an actor-goal `FAIMoveRequest` with pathfinding enabled and partial
  paths disabled;
- listens to the matching path-following request only;
- turns the owning actor toward the still-registered target at a bounded rate;
- submits the resolved Published template only after movement and facing;
- enforces per-step and whole-plan timeouts;
- supports explicit cancellation and stable debug/error state.

Action-only turns retain their existing immediate execution behavior. A second
behavior is rejected while one is active instead of silently replacing it.

## Project settings

The following values remain UE-authoritative under `Runtime | Behavior`:

- navigation enablement;
- default AI-controller spawning;
- default, minimum, and maximum acceptance radius;
- movement and whole-plan timeout;
- target-facing timeout, turn rate, and tolerance;
- coordinator tick interval.

## Automation

Automation filter: `LLMNPCActionLayer.Phase6.Behavior`

The contract tests cover plan construction, target whitelist enforcement,
self-target rejection, order validation, model-radius clamping, and Mock
Provider scene-target selection.

## PIE setup and acceptance

The NPC must be a Pawn/Character with an AI controller class and movement
component. The test map must contain a covering `NavMeshBoundsVolume`, and the
player must be registered under an opaque ref such as `player.main`.

The first visual acceptance scenario is:

```text
"come here and wave"
  -> move_to player.main
  -> stop inside the clamped radius
  -> face player.main
  -> play gesture.wave.asset.manny.v1
```

PIE visual quality remains human-reviewed. Objective verification uses
`GetBehaviorDebugState`, motion debug state, final actor distance, final facing
error, and the output log.

## Remaining Phase 6 work

- create the demo Blueprint/NavMesh wiring for the first real movement loop;
- add interaction-channel arbitration between locomotion and external combat;
- wait for authored animation completion when a later behavior step depends on
  it;
- add reviewed upper-body overlays while locomotion is active;
- add richer user-facing animation alias and behavior configuration tools.
