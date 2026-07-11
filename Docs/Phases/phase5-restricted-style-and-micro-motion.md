# Phase 5: Restricted Style And Micro Motion

## Release

- Plugin version: `0.7.1`
- Engine: Unreal Engine 5.3.2
- Request schema: `llmnpc.turn_request.v2`
- Prompt contract: `llmnpc.selection_prompt.v2`
- Automation filter: `LLMNPCActionLayer.Phase5`

## Delivered

- Built-in `neutral`, `friendly`, `subtle`, and `excited` style presets.
- Emotion-to-Style and Personality-to-Style mapping in UE runtime code.
- Recommended amplitude, speed, duration, and style in filtered candidates.
- UE-generated deterministic action seeds; providers cannot submit a seed.
- Policy-bounded amplitude, speed, oscillator frequency, phase, offset, and
  blend-time variation in `FLLMNPCTemplateCompiler`.
- Stable weighted Template Variant resolution by public action, skeleton,
  Style Tag, and seed.
- Published `gesture.wave.right.manny.subtle.v1` style-specific Variant, based
  on the manually reviewed Waving FK reconstruction.
- Default runtime Wave resolves only to the reviewed FK reconstruction; the
  earlier hand-anchor experiment remains internal-only.
- Semantic procedural-template mirroring from right-hand controls to left-hand
  controls.
- Left arm IK, local offsets, palm orientation, open-hand pose, point-hand pose,
  mirrored FK, motion channels, snapshot merge, and AnimNode execution.
- Right-hand occupancy keeps mirror-capable Wave available and marks it for
  UE-side left-hand execution.
- Local breathing, head sway, occasional low-amplitude nod, and ambient gaze.
- Formal head/chest/gaze channels suppress conflicting micro motion.
- Ambient style drives micro-motion amplitude and gaze engagement.
- Phase 3 Draft import supports optional Variant and randomization policy data.

## Style Flow

```text
Emotion + Personality + Relationship
  -> recommended Style Tag
  -> candidate modifier recommendations
  -> provider selects only allowed public action and Style
  -> UE contextual policy
  -> deterministic action seed
  -> style-aware Published Variant
  -> template policy clamp
  -> bounded curve preset and deterministic jitter
  -> optional semantic mirror
  -> validated Motion Plan
```

## Determinism

The action seed is derived from:

```text
Session ID + Request ID + NPC ID + Public Action ID
```

The same seed, template version, skeleton profile, Style Tag, and modifiers
produce the same compiled frequency, phase, timing, and resolved Variant.
Different seeds may vary only within local Template Policy.

## Template Variant Rules

- Variants share a `PublicActionId` but retain unique internal `TemplateId`s.
- `VariantStyleTags` make a Variant style-specific.
- Style-specific Variants take precedence over generic Variants.
- Weighted selection is deterministic and sorted by internal Template ID before
  sampling.
- Only locally Published and runtime-selectable Variants enter the index.

## Mirror Rules

- Mirror must be enabled by the selected template's Modifier Policy.
- Compiler maps semantic controls and anchors; it does not accept raw bones
  from providers.
- Unsupported controls fail compilation with a stable error.
- Hand IK, local offsets, palm target, open fingers, and point fingers retain
  semantic mirror support.
- Wave FK values remain source-side semantic data until the AnimNode reflects
  component-space rotations across Manny's skeletal X axis. This avoids
  incorrect Euler sign mirroring between different left/right local bases.
- Normalized hand-pose controls are not weakened by gesture amplitude scaling.

## Micro Motion Rules

- Micro motion is generated locally by `FLLMNPCMicroMotionScheduler`.
- It never submits a model request or a runtime raw Motion Plan.
- Breathing and weight nuance yield to the formal chest channel.
- Head sway and micro nod yield to the formal head channel.
- Ambient gaze yields to either formal head or gaze channels.
- Gaze targets come only from actors already registered with Motion Component.
- Seeded target switching is deterministic.

## Security Boundaries

- Providers still receive no bones, controls, transforms, world coordinates,
  Keyframe arrays, oscillator frequency, phase, or random seed.
- Style presets are compiled code; models select only allowed Style Tags.
- Randomization limits are authored in reviewed template policy.
- Variant internal IDs are never exposed as public candidate IDs.
- Mirror recommendation is a boolean UE policy result, not body-control data.
- Motion Validator, target validation, channel scheduling, IK, and AnimNode remain
  final authorities.

## Acceptance Results

- Excited emotion maps to `excited`; shy personality maps to `subtle`.
- Same action identity produces the same seed.
- Same seed reproduces Variant and curve parameters.
- Different seeds produce bounded curve differences.
- Shy Wave resolves `gesture.wave.right.manny.subtle.v1`.
- `right_hand_busy` selects a left-hand mirrored Wave when the left hand is free.
- Mirrored Wave sampling activates left-arm FK, leaves both hand IK solvers
  inactive, and produces a geometrically symmetric reference-pose hand path.
- Local micro motion produces breathing and gaze without model calls.
- Formal channels suppress conflicting micro motion.
- Provider context contains recommendations but no curve or seed authority.

## Verification

- Phase 5 automation: 6/6 passed.
- Phase 0-5 regression automation: 34/34 passed.
- Editor/Game builds, Windows Cook, and runtime map smoke are required before the
  release commit and tag.
