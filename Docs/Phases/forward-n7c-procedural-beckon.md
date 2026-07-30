# Forward N7-C: Procedural Beckon

## Status

- Implementation gate: passed
- Human online PIE gate: passed
- Publication gate: passed
- Published runtime reuse gate: passed
- Target release: `v0.10.0-rc.4`
- Target skeleton: UE5 Manny only

N7-C is complete. The online-generated motion passed human visual review,
completed the explicit Quality, Previewed, HumanApproved, and Publish gates,
and was selected and executed again through the strict real-provider runtime
path.

## Goal

Add a target-aware, online-authored Beckon family for UE5 Manny without giving
the model access to bones, transforms, wrist rotations, IK anchors, or concrete
Actor references. The model directs the gesture through one semantic
`hand.beckon` primitive. Unreal validates the request, resolves the scene
target, places and orients the arm, drives the fingers, applies context
modifiers, and owns interruption and recovery.

## Protocol versions

- Primitive Registry: `llmnpc.motion_primitives.v3`
- Recipe Compiler: `llmnpc.motion_recipe_compiler.v5`
- Authoring Prompt: `llmnpc.motion_recipe_authoring_prompt.v5`
- Control Manifest: `llmnpc.control_manifest.v2`
- Plugin release version: `0.10.0-rc.4`

Prompt v3 and v4 provenance remains readable for existing reviewed templates.
New online Recipe generation uses prompt v5 and an immutable Authoring
Contract.

## Model-facing contract

The Workbench exposes `Procedural Beckon`, backed by contract
`gesture.beckon.procedural`.

The contract requires:

- public action: `gesture.beckon`
- intent: `attract_attention`
- exactly one primitive: `hand.beckon`
- side: `left` or `right`
- exactly one semantic target slot: `primary`
- no helper, timing, raw control, or transform primitives

The model may set only:

- `amplitude`: `0.3..1.0`
- `speed`: `0.7..1.3`
- `cycles`: integer `1..4`
- `curl_amount`: `0.35..0.95`
- `reach`: `0.35..0.78`
- `height`: `0.3..0.8`
- duration: `0.9..3.2` seconds

The prompt explains the target-slot contract but never includes an Actor name,
world coordinate, bone name, or solver detail. Missing, invented, or
unauthorized target slots fail closed.

## Unreal execution

`solver.hand_beckon.manny.v1` compiles the primitive into four tracks on the
selected side:

- hand IK reach
- constrained palm target
- calibrated relaxed finger pose
- calibrated curl finger pose

The relaxed and curl curves are complementary and synchronized across the
requested cycle count. The palm uses a constrained target-facing,
palm-up/outward basis. Arm placement remains inside the Manny reach envelope;
finger transforms remain Unreal-owned calibrated poses.

The generated template records:

- dynamic target tracking enabled
- bounded reach, height, lateral, palm, and finger policies
- `FadeOut` target-loss policy with a `0.18` second fade
- semantic left/right mirroring
- side-specific occupied-hand conflicts
- interruption and recovery metadata

Target validity gates both arm placement and finger motion. When the target is
lost, the hand and fingers fade together instead of leaving a detached curl
pose. Runtime `ReachScale` now affects the actual IK reach distance, so
near/far context refinement is no longer metadata-only.

## Workbench flow

1. Open `LLM NPC > Template Workbench`.
2. Open `Generate`.
3. Select Manny profile `ue5_manny.v1`.
4. Select Recipe Contract `Procedural Beckon`.
5. Keep or refine the desired-action description.
6. Click `Generate Online`.
7. Start PIE with a Manny preview NPC and a distinct player Pawn.
8. Run `Preflight and Preview`.
9. Inspect the live motion before recording `Human Visual Pass`.
10. Create the Generated Draft.
11. Complete Quality, Previewed, HumanApproved, and explicit Publish.

The Workbench binds the contract's semantic `primary` slot to the current
player Pawn only for the authoring preview. A selected Manny actor cannot also
serve as its own target.

## Deterministic verification

`LLMNPCActionLayer.ForwardN7C` covers:

- Manny capability and exported Schema exposure
- immutable target-aware Authoring Contract
- target-required and unknown-target rejection
- side and parameter allowlists
- occupied-hand and two-hand conflict policy
- deterministic four-track compilation
- target propagation to arm, palm, and finger tracks
- bounded relaxed/curl curves
- semantic mirror compilation
- real Generated Draft creation and Quality evaluation

Current machine result:

- Editor Development build: passed
- `LLMNPCActionLayer.ForwardN7C`: 6 of 6 passed
- `LLMNPCActionLayer`: 118 of 118 passed
- checked-in Manny capability artifact: refreshed
- checked-in Motion Recipe Schema: refreshed

## Human acceptance gate

Automation cannot decide whether a body gesture reads naturally. Before this
stage is published and tagged, the user must verify in PIE:

- the arm beckons toward the player rather than merely moving forward
- near, far, and elevated targets produce bounded readable refinements
- a moving target does not cause visible wrist flipping or arm jitter
- the palm faces naturally upward/outward
- relaxed-to-curl finger cycles are readable and move with the hand
- neither elbow nor wrist causes visible skin collapse
- a busy right hand mirrors the gesture to the left when allowed
- target loss, interruption, and recovery do not leave a residual pose

Only the user records the visual verdict. Machine validation and the model do
not self-approve this gate.

### First visual review

The first online generation used DeepSeek and produced a valid two-cycle,
right-hand Recipe with default bounded parameters. Full Preflight passed, but
the user recorded `Human Visual Fail`: the elbow connection between the upper
and lower arm moved stiffly and occasionally appeared to teleport.

The failure had two Unreal-side causes:

- the effector position blended in, but the two-bone IK pole influence did not
  enter with the same weight, so a low nonzero IK alpha could immediately
  rearrange the elbow
- the semantic Beckon reach was passed directly into the shared reach-distance
  mapping, placing Manny near full arm extension where the elbow plane becomes
  poorly conditioned

The correction makes pole influence blend from the current elbow pose, fades
profile-pole influence near a pole/effector collinearity, maps Beckon reach to
a Manny-specific bent-elbow band, and preserves a small mirrored outward elbow
offset. Regression coverage now asserts that crossing the forward pole does
not flip the elbow and that a one-percent IK blend moves the elbow by less
than one centimeter.

The corrected motion passed the user-owned online PIE visual gate. The user
recorded `Human Visual Pass`.

### Sandbox-to-Draft identity correction

The first attempt to send the accepted Recipe to a Generated Draft failed with
`LLMNPC_RECIPE_DRAFT_KINEMATIC_HASH_MISMATCH`. The motion and compiled Recipe
hashes were unchanged. The mismatch came from target-reference naming:

- Sandbox compilation replaced semantic slot `primary` with the transient
  runtime alias `authoring_preview_target`
- Draft and Quality recompilation used the persistent semantic placeholder
  `primary`
- the kinematic Plan hash included the target-reference string even though the
  alias did not change any sampled motion

Sandbox preview, Draft creation, and Quality recompilation now share one
canonical authoring target reference derived from the semantic target slot.
The runtime preview actor still resolves that semantic reference to the actual
PIE player target; no Actor identity or transform enters the persistent Recipe.

Regression coverage now passes the real Sandbox compiled and kinematic hashes
into Generated Draft creation and asserts that the Draft preserves the
Sandbox-approved kinematic identity.

## Publication closure

The accepted online Recipe completed Quality, Previewed, HumanApproved, and
explicit Publish on 2026-07-30.

Published Public Action:

```text
/Game/LLMNPCActionLayer/PublicActions/Published/PA_Gesture_Beckon_Draft.PA_Gesture_Beckon_Draft
```

Canonical Public Action source:

```text
Resources/PublicActions/Published/gesture_beckon_1_0_0_r1.json
```

Published Manny Motion Template:

```text
/Game/LLMNPCActionLayer/MotionTemplates/Published/MT_Beckon_Manny_Procedural_Generated_145355d29de7.MT_Beckon_Manny_Procedural_Generated_145355d29de7
```

Canonical Motion Template source:

```text
Resources/Templates/Published/gesture_beckon_manny_procedural_generated_145355d29de7_1_0_0_r1.json
```

The Public Action is `gesture.beckon` version `1.0.0`, revision 1, with content
hash `md5:03e4efe3de2e3959b55195c3466c77d1`. The Published template is
`gesture.beckon.manny.procedural.generated.145355d29de7`, with catalog content
hash `md5:94e0646231ff701f6564247776520a4b`, source Recipe hash
`md5:10b324222a58859ca491a5da009c01a4`, and passing kinematic report hash
`md5:dd99fe8713921e7a200461182a7d926b`.

## Published runtime verification

A fresh PIE session rebuilt the runtime catalog with 10 Published templates,
6 Public Actions, 1 Manny skeleton profile, and zero indexing errors.
`SubmitPublishedTemplate("gesture.beckon", ...)` returned `true`, activated
the resolved runtime clip
`gesture.beckon.manny.procedural.generated.145355d29de7:friendly:145355`,
reported an empty `LastValidationError`, and cleared `ActiveClipId` after
completion.

The strict online Motion Test Console gate then received:

```text
Invite the player to come closer with a friendly beckoning gesture.
```

The real provider selected `gesture.beckon`, resolved
`gesture.beckon.manny.procedural.generated.145355d29de7`, executed the action
without local fallback, and passed the strict provider/config identity gate.
The user completed the corresponding PIE observation and confirmed that the
Published runtime motion matched the approved Beckon.

Final release verification:

- `LLMNPCDemoEditor Win64 Development`: passed
- `LLMNPCActionLayer.ForwardN7C`: 6 of 6 passed
- `LLMNPCActionLayer`: 118 of 118 passed
- JSON and plugin descriptor UTF-8 validation: passed
- Published catalog startup: 10 templates, 6 Public Actions, 1 profile, 0 errors
