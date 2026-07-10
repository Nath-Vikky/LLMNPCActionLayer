# LLM NPC Action Layer

Data-driven LLM procedural NPC gesture layer for Unreal Engine 5.3.

The plugin keeps large language models away from raw skeletal control. Runtime models select a published action and constrained modifiers. Unreal Engine resolves the skeleton-specific template, validates it, schedules its channels, samples motion tracks, and applies the procedural pose through a post-process animation path.

## Runtime Flow

```text
public_action_id + constrained modifiers
  -> ULLMNPCTemplateLibrarySubsystem
  -> published ULLMNPCMotionTemplate
  -> FLLMNPCTemplateCompiler
  -> ULLMNPCMotionValidator trust boundary
  -> channel / priority / interruption scheduler
  -> FLLMNPCMotionSampler
  -> FLLMProceduralPoseSnapshot
  -> ULLMNPCPostProcessAnimInstance
  -> FAnimNode_LLMProceduralPose
  -> final skeletal pose
```

## Core Classes

- `ULLMNPCMotionComponent`
- `ULLMNPCControlManifest`
- `ULLMNPCMotionValidator`
- `ULLMNPCTemplateLibrarySubsystem`
- `ULLMNPCMotionTemplate`
- `ULLMNPCSkeletonProfile`
- `FLLMNPCMotionSampler`
- `ULLMNPCPostProcessAnimInstance`
- `FAnimNode_LLMProceduralPose`
- `UAnimGraphNode_LLMProceduralPose`

## Built-In Controls

```text
head.pitch
head.yaw
head.roll
chest.pitch
chest.yaw
chest.roll
right_hand.ik
right_hand.local_offset.x
right_hand.local_offset.y
right_hand.local_offset.z
right_hand.palm_target
right_upperarm.pitch
right_upperarm.yaw
right_upperarm.roll
right_lowerarm.pitch
right_lowerarm.yaw
right_lowerarm.roll
right_hand.pitch
right_hand.yaw
right_hand.roll
right_fingers.open
right_fingers.point
gaze.target
```

The direct right-arm FK controls are internal-only. Published, reviewed templates may use them; runtime model plans may not.

## Published Templates

```text
gesture.nod.manny.v1
gesture.wave.right.manny.fk.v1
gesture.wave.right.manny.procedural.v1
```

Runtime model selection exposes the skeleton-independent public IDs `gesture.nod` and `gesture.wave.right`. The faithful FK wave remains available by exact ID for trusted debug and review workflows, but is hidden from model candidate selection.

## Main Blueprint API: Motion

- `SubmitMotionPlanJson`
- `SubmitMotionPlan`
- `SubmitPublishedTemplate`
- `RequestMotionPlanFromContext`
- `RegisterTarget`
- `SubmitSampleMotionPlanJson`
- `BuildSampleMotionPlanJson`
- `GetDebugState`
- `TestNod`
- `TestWave`

## Default HTTP Endpoint

```text
http://localhost:8787/npc/motion-plan
```

The earlier `ULLMNPCActionComponent` API is still present as a compatibility/prototyping layer, but the primary path is now Published Template-driven.

The plugin ships its default post-process AnimBP, Manny skeleton profile, and published templates as cooked plugin content. Motion templates are also retained as JSON authoring sources under `Resources/Templates`.
