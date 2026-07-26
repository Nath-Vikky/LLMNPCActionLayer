# Forward N3: Context Modifier

## Status

- Planned release: `v0.10.0-beta.1`
- Primary skeleton profile: `ue5_manny.v1`
- Catalog hash: `md5:8a9297fc3b842642c8c5a3211064caf6`
- Mapping profile: `manny.default.v1`
- Online evaluation: passed on 2026-07-26
- Final human PIE visual gate: passed on 2026-07-26
- Multiplayer scope: deferred

## Delivered

- A deterministic `FLLMNPCContextModifierResolver` between model selection
  and template compilation.
- A versioned Manny modifier mapping profile for personality, emotion, and
  relationship inputs.
- Extended template policy for reach, height, lateral motion, cycle count,
  gaze, palm, fingers, torso, blend timing, target tracking, target loss, and
  obstacle response.
- UE-local execution context for target geometry, locomotion, hand and upper
  body occupancy, available space, obstacle sweeps, LOD, and base pose.
- Fixed-order modifier resolution with a serializable, human-readable trace.
- Mirror or fallback behavior for occupied hands.
- Walking reduction and fail-closed handling for incompatible movement states.
- Near, far, and high-target adaptation.
- Dynamic target interpolation with linear and angular speed limits, persistent
  teleport detection, and cancel, hold, or fade target-loss policies.
- Obstacle-aware amplitude and reach reduction, side switching, and
  cancellation when clearance is unsafe.
- Runtime debug state and overlay fields for execution context, resolved
  modifiers, fallback result, and resolution trace.
- Strict N3 Draft import fields and JSON schema coverage.
- A sanitized real-provider context comparison suite.

## Trust Boundary

The online model receives public action cards and model-visible semantic
context. It may select a Public Action and suggest the existing bounded model
parameters:

```text
target_ref
amplitude
speed_scale
duration_scale
style
mirror
random_seed
```

The model does not receive bone names, compact pose indices, transforms,
quaternions, Control Rig internals, asset paths, obstacle sweeps, or raw
execution geometry. Distance, height, occupancy, movement, collision, target
tracking, template policy, and Manny constraints remain UE authority.

The resolver order is fixed:

```text
Template Defaults
  -> Validated Model Request
  -> Style Preset
  -> Personality
  -> Emotion
  -> Relationship
  -> Movement And Occupancy
  -> Target Geometry
  -> Obstacle Adaptation
  -> Template Policy Clamp
  -> Skeleton Capability Clamp
```

Every effective change records stage, modifier, operation, before value,
contribution, after value, and reason. Fallback is an explicit result, never a
silent unsafe substitution.

## Manny Policy Migration

Run the idempotent migration with:

```text
LLMNPC.MigrateMannyN3Context
```

It creates or refreshes:

```text
/LLMNPCActionLayer/LLMNPC/Context/MP_Manny_Default_v1
```

It also upgrades the shipped Nod, Point, FK Wave, procedural Wave, and subtle
Wave policies. The project-owned animation-asset Wave is upgraded when present.
Point and Wave can now mirror when the selected hand is unavailable.

## Automated Verification

```text
Focused Forward N3 suite: 4/4
Full LLMNPCActionLayer suite: 77 succeeded
Failures: 0
Not run: 0
```

N3 coverage includes:

- personality, emotion, and relationship mapping;
- near, far, and high-target geometry;
- hand occupancy, mirror, fallback, movement, and obstacle policy;
- template and skeleton clamping;
- fixed-seed deterministic plan output;
- dynamic target linear speed, angular speed, and teleport fade;
- zero-inclusive weight ranges in the strict Draft importer;
- compatibility with the historical Phase 4 candidate filtering contract.

## Online Verification

Provider: `deepseek_direct_editor`

Model: `deepseek-v4-flash`

The real-model suite runs six context cases three times each. Each sample keeps
the model suggestion fixed while comparing a neutral UE baseline with the
actual UE execution context:

```text
point_near_target: 3/3
point_far_target: 3/3
point_high_target: 3/3
wave_selected_hand_occupied: 3/3
wave_excited: 3/3
wave_while_walking: 3/3
```

Final metrics:

```text
Status: passed
Samples: 18/18
Policy bound rate: 1.0
Trace complete rate: 1.0
Adaptation pass rate: 1.0
Final value difference rate: 1.0
Average latency: 2.586 seconds
Average tokens: 2790.6
```

Raw requests, raw responses, credentials, and headers were not persisted. The
secret-pattern scan found zero matches. The local report is:

```text
Saved/LLMNPCActionLayer/ForwardN3/Reports/context_modifier_20260726_030026.json
```

`Saved` remains outside the release.

Run the suite from an Editor session with `env.txt` loaded:

```text
LLMNPC.RunMannyN3ContextModifierSmoke
```

For unattended execution, add:

```text
-LLMNPCContextSmokeExit
```

## Human PIE Gate

Automation proves selection, bounds, trace, determinism, and compilation. It
does not declare visual quality. Before tagging the phase, a human must inspect
the same Manny action under:

- neutral, near, far, and high target placement;
- right-hand occupancy and left-side mirroring;
- neutral and excited emotion;
- stationary and walking locomotion;
- a slowly moving target and a teleported or removed target;
- open space and a close arm-side obstacle.

The reviewer should confirm that the action remains recognizable, transitions
cleanly, keeps hands and fingers attached, avoids visible snapping, follows the
correct target, respects the free hand, and returns to the base pose.

The final Manny PIE review passed all eight required checks on 2026-07-26:

```text
near target point: passed
far target point: passed
high target point: passed
dynamic target tracking: passed
excited wave: passed
occupied-right-hand mirror: passed
walking-context wave: passed
stop and base-pose recovery: passed
```

Visual reviewer: project owner.

## Release Gate

The human PIE matrix passed, so this phase is eligible for
`v0.10.0-beta.1`. Any later change to modifier mapping, template policy,
context collection, target tracking, obstacle adaptation, skeleton
constraints, or model-facing context requires a new full regression and
online context suite run.
