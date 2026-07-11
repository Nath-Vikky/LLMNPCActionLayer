# Phase 6 Animation Asset Runtime

Version: `0.7.3`

This slice connects reviewed Animation Sequence and Animation Montage assets to the existing high-level template selection path.

## Runtime boundary

- The model selects only a Published template ID or public action ID.
- The runtime template owns the soft animation asset reference and playback policy.
- Raw asset paths are not added to the model request or response schema.
- Animation assets are validated for supported type, exact skeleton, Root Motion policy, start position, play rate, explicit channels, and hard timeout.
- Runtime mirroring is rejected. A mirrored animation must be a separately reviewed template variant.

## Playback policy

Animation Asset templates define:

- Slot name;
- blend-in and blend-out time;
- start position and maximum duration;
- looping and interruption policy;
- whether other montages may be stopped;
- whether reviewed Root Motion is allowed.

Animation Sequences are played as dynamic montages. Animation Montages retain their authored slot tracks and use the template blend-in, speed, interruption, and timeout policy.

## Scheduling

- Animation assets share the existing semantic interaction channels.
- A full-body asset blocks active procedural channels.
- Procedural plans wait while a conflicting asset channel is active.
- A conflicting asset can replace another asset only when the active template is interruptible.
- Template cooldown is applied when asset playback begins.

## Verification

- Automation filter: `LLMNPCActionLayer.Phase6.AnimationAsset`
- The demo `Waving` sequence is used as a validated asset-resolution fixture.
- Editor/Game builds, full plugin automation, runtime-map smoke launch, and Windows cook remain release gates.

## Editor setup required for the first visual test

Create one `LLMNPCMotionTemplate` under `/Game/LLMNPCActionLayer/MotionTemplates`, set `Kind` to `AnimationAsset`, reference `/Game/LLMNPC/Animation/Waving`, assign an explicit channel such as `full_body`, configure `DefaultSlot`, complete provenance and validation data, and publish it through the existing authoring workflow.
