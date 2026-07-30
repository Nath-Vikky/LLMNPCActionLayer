# Forward N7-B: Procedural Clap

## Goal

Add a second online-authored Motion Recipe family for UE5 Manny:
`gesture.clap`. The model selects one semantic `hands.contact` primitive and
bounded expressive parameters. Unreal owns all arm placement, palm alignment,
contact separation, timing curves, finger pose, validation, and recovery.

The existing reviewed Mixamo `Clapping` AnimationAsset remains the Published
reference variant. The procedural variant joins the same Public Action only
after a separate human review and publish cycle.

## Protocol versions

- Primitive Registry: `llmnpc.motion_primitives.v2`
- Recipe Compiler: `llmnpc.motion_recipe_compiler.v4`
- Authoring Prompt: `llmnpc.motion_recipe_authoring_prompt.v4`
- Control Manifest: `llmnpc.control_manifest.v2`
- Plugin release version: `0.10.0-rc.3`

Prompt-v3 provenance remains readable for already reviewed assets. New online
generation requests must use prompt v4 and carry an immutable Authoring
Contract ID.

## Model-facing contract

The Workbench exposes `Procedural Clap`, backed by contract
`gesture.clap.procedural`.

The contract requires:

- public action: `gesture.clap`
- intent: `applaud`
- exactly one primitive: `hands.contact`
- no scene target
- no helper, hold, pause, transition, or timing primitive

The model may set only:

- `amplitude`: `0.3..1.0`
- `speed`: `0.7..1.3`
- `cycles`: integer `1..3`
- `contact_height`: `0.35..0.75`
- `separation`: `0.2..1.0`
- `palm_openness`: `0.5..1.0`
- recipe duration: `0.8..3.2` seconds

Duration and cycles define the contact cadence. `speed` adjusts bounded stroke
velocity and energy without exposing or replacing Unreal's timing curve.

Bone names, anchors, controls, transforms, rotations, IK solvers, asset paths,
and concrete target references are absent from the model schema.

## Unreal execution

`solver.hands_contact.manny.v3` compiles the semantic primitive into:

- right and left hand IK anchors
- mirrored lateral separation keyframes
- dedicated right and left palm-facing controls aimed at a shared contact center
- a Manny-calibrated bilateral contact hand pose with all 30 finger joints
- a sustained entry, contact, and exit envelope for cyclic hand placement

The evenly timed contact keyframes approach, touch, reverse smoothly, and
recover for each cycle. Placement and palm orientation ease in over the first
20 percent, remain fully available through every contact, and ease out over
the final 20 percent. This avoids weakening the first and last contact of a
multi-cycle Clap.
The right hand remains on the negative Manny lateral axis and the left hand
remains on the positive axis, so generated tracks cannot cross through one
another and both hands return to the base pose at Recipe boundaries.

Palm normals use only the signed Manny lateral component of the contact
direction. Forward movement and hand height cannot rotate or flip the palm.
The two finger axes share a calibrated forward-and-up direction derived from
the reviewed `Clapping` sequence instead of aiming at the opposite hand.

Internal Manny anchors were calibrated from the reviewed
`/Game/LLMNPC/Animation/Clapping.Clapping` sequence:

- right hand base: `(-4, 27, 18)` relative to `spine_03`
- left hand base: `(4, 27, 18)` relative to `spine_03`
- shared palm target: `(0, 27, 18)` relative to `spine_03`
- contact hand distance: approximately `8 cm`
- open hand distance: approximately `22..24 cm`

These values are Unreal-owned calibration. The procedural template does not
play or depend on the Mixamo animation asset.

## Safety and conflicts

`hands.contact` is Authoring-only and is rejected when any of these states are
active:

- `left_hand_busy`
- `right_hand_busy`
- `two_hand_interaction`
- `dead`
- `ragdoll`
- `stunned`

The primitive reserves both `arm_ik` and both `hand_pose` channels. Runtime
selection still sees only HumanApproved/Published templates; an online Recipe
cannot enter the product catalog directly.

## Workbench flow

1. Open `LLM NPC > Template Workbench`.
2. Open `Generate`.
3. Select Manny profile `ue5_manny.v1`.
4. Select Recipe Contract `Procedural Clap`.
5. Keep or refine the generated desired-action description.
6. Click `Generate Online`.
7. Run `Preflight and Preview` against the Manny PIE actor.
8. Record `Human Visual Pass` only after visual inspection.
9. Create the Generated Draft.
10. Complete Quality, Previewed, HumanApproved, and Publish as a new
    procedural variant of `gesture.clap`.

Rejected procedural Clap drafts can use `Revise Online`. The revision remains
locked to the same Clap contract and Public Action.

## Deterministic verification

`LLMNPCActionLayer.ForwardN7B` covers:

- model-safe capability and schema exposure
- immutable Clap authoring contract
- same-Public-Action reference-template prioritization
- intent and primitive allowlist enforcement
- occupied-hand rejection
- parameter-range rejection
- `0.8..3.2` primitive-duration enforcement
- full-Recipe timing coverage enforcement
- deterministic compiler identity
- no concrete target or raw wrist Euler controls
- mirrored non-crossing separation tracks
- bounded contact/open distances
- opposing palm normals at the shared contact center and open fingers
- Published catalog derivation preserving the passing Quality report
- configured `BP_LLMNPC_Manny` authoring readiness

Refresh checked-in model artifacts with:

```text
LLMNPC.ExportMotionRecipeSchema
LLMNPC.ExportMannyCapability
```

The second command is export-only and does not modify the Manny Profile asset
or its approved validation baseline.

## Human acceptance gate

Automation proves structure and bounded geometry, not visual quality. Before
this stage is committed and pushed as accepted, inspect the online-generated
procedural Clap in PIE against the Published Mixamo baseline:

- both palms face one another instead of folding or hanging
- both elbows bend naturally without skin collapse
- each requested cycle has readable approach, contact, and separation
- fingers follow the palms and remain naturally open
- both arms remain visually balanced
- interruption and idle recovery remain smooth

PIE visual status: passed by the user on 2026-07-30. The procedural result is
acceptable for this release, with minor hand-detail polish intentionally
deferred rather than treated as a blocking defect.

### First visual review

The first online procedural preview completed Full Preflight, but the backs of
the hands faced one another during contact. The review was correctly recorded
as `Visual Fail`.

The correction separates two orientation meanings that previously shared one
control:

- `right_hand.palm_target` and `left_hand.palm_target` continue to aim the
  fingers for Point and Wave.
- `right_hand.palm_facing` and `left_hand.palm_facing` orient the actual palm
  normals toward the Clap contact center while keeping the fingers upward.

The same review also exposed a false
`LLMNPC_AUTHORING_QUALITY_REPORT_STALE` error when previewing the Published
AnimationAsset reference. Published catalog metadata is now excluded from the
stable Quality-content hash, because publishing derives that metadata without
changing the reviewed motion.

### Second visual review

The dedicated contact controls removed the hand-back collision, but the
second preview exposed two remaining problems:

- the wrists visibly flipped while the hands moved into the contact pose
- the finger axes still aimed toward one another instead of remaining
  together in an upward Clap pose

The reference Animation Sequence shows nearly fixed opposing lateral palm
normals throughout the action. Its finger axes point diagonally forward and
up, rather than directly at the contact center. Solver v2 now uses that stable
basis and a sustained cyclic envelope. Point and Wave retain their original
finger-aim controls.

Post-second-correction machine status:

- `LLMNPCActionLayer.ForwardN7B`: 9 passed
- `LLMNPCActionLayer`: 111 passed

### Third visual review

The stable palm basis removed the major hand flip and direction errors. The
next online preview was broadly readable, but the fingers remained curled
instead of forming the naturally extended hand shape visible in the reviewed
AnimationAsset reference.

The cause was the shared `fingers.open` calibration. It intentionally applies
small additive offsets for Wave and Point compatibility; on Manny it cannot
fully counter the curved reference finger joints required by Clap. Solver v3
therefore uses dedicated `right_fingers.contact` and
`left_fingers.contact` controls. Their 30 joint rotations were reconstructed
from the constant local finger pose in
`/Game/LLMNPC/Animation/Clapping.Clapping`. The left hand is calibrated
independently instead of negating the right-hand Euler values. The existing
Open, Point, Relaxed, and Curl poses remain unchanged.

Post-third-correction machine status:

- `LLMNPCActionLayer.ForwardN7B`: 10 passed
- `LLMNPCActionLayer`: 112 passed
- corrected online Manny PIE finger review: passed
- human assessment: acceptable for release, with remaining detail polish

## Publication closure

The accepted online Recipe completed Quality, Previewed, HumanApproved, and
explicit Publish on 2026-07-30.

Published asset:

```text
/Game/LLMNPCActionLayer/MotionTemplates/Published/MT_Clap_Manny_Procedural_Generated_f4005a3e5b3f.MT_Clap_Manny_Procedural_Generated_f4005a3e5b3f
```

Canonical plugin source:

```text
Resources/Templates/Published/gesture_clap_manny_procedural_generated_f4005a3e5b3f_1_0_0_r1.json
```

The Published template has catalog content hash
`md5:d9ddab83429b3ddac9df9983dfa7a24c`, retains the passing Quality report and
online provider provenance, and joins the already Published
`PA_Gesture_Clap` Public Action.

The first Publish attempt correctly failed closed because the Workbench had
added `rhythmic` as a VariantStyle tag even though that value was absent from
the controlled Action Vocabulary. The generated template now uses only
`neutral` and `excited`. Recipe Draft creation validates all catalog tags
before creating an asset, and Quality independently records both
`action_vocabulary` and `template_vocabulary_tags` checks. Invalid generated
tags therefore fail during authoring instead of surfacing for the first time
at Publish.

Final verification:

- Editor build: passed
- `LLMNPCActionLayer.ForwardN5.Editor.RecipeDraftQuality`: passed
- `LLMNPCActionLayer.ForwardN7B`: 10 passed
- `LLMNPCActionLayer`: 112 of 112 passed
- Published asset and canonical plugin JSON: verified
