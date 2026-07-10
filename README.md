# LLM NPC Action Layer

Data-driven LLM procedural NPC gesture layer for Unreal Engine 5.3.

The plugin keeps large language models away from raw skeletal control. Runtime models return dialogue text plus a published action ID and constrained modifiers. Unreal Engine resolves the skeleton-specific template, validates it, schedules its channels, samples motion tracks, and applies the procedural pose through a post-process animation path.

## Runtime Flow

```text
player text / story command
  -> ULLMNPCDialogueComponent
  -> Mock, Backend Proxy, or editor-only DeepSeek provider
  -> strict llmnpc.model_turn.v1 parser
  -> public_action_id + constrained modifiers
  -> ULLMNPCBehaviorCoordinator
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
- `ULLMNPCDialogueComponent`
- `ULLMNPCConversationSession`
- `ULLMNPCBehaviorCoordinator`
- `ULLMNPCChatWidget`
- `ILLMNPCModelProvider`
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

## Main Blueprint API: Dialogue

- `SendPlayerMessage`
- `CancelActiveRequest`
- `ResetConversation`
- `SetProviderKind`
- `SetMotionComponent`
- `RegisterTarget`
- `CreateChatWidget`
- `GetDebugState`

Add `LLM NPC Motion Component` and `LLM NPC Dialogue Component` to the NPC actor. Set the Dialogue provider to `Mock` for a fully local first test. Enable `Auto Create Chat Widget`, or call `CreateChatWidget` from the player-facing gameplay layer. The shipped `WBP_LLMNPCChat` derives from the native widget and needs no Blueprint graph.

The Phase 2 Mock recognizes explicit English and Chinese Nod/Wave commands. It also maps greetings to the optional Wave behavior. Multi-turn history is bounded and only public Published candidates are sent to a remote provider.

## Model Providers

- `Mock`: deterministic, local, and the default.
- `BackendProxy`: recommended runtime path; POSTs the turn context to `BackendProxyEndpoint`.
- `DeepSeekDirectEditorOnly`: development-only direct adapter. It is disabled unless explicitly enabled in Project Settings and reads `DEEPSEEK_API_KEY` only from the editor process environment.

The backend may return either a `llmnpc.model_turn.v1` object or `{ "decision": <model-turn-object> }`. The model response is parsed with unknown-field rejection and then checked against the local Published template library. Invalid action IDs preserve displayable assistant text but cannot drive the body. Direct provider credentials are compiled out of non-editor targets, and runtime builds are expected to use a trusted backend proxy.

Default dialogue backend endpoint:

```text
http://localhost:8787/v1/npc/turn
```

## Legacy Motion-Plan Endpoint

```text
http://localhost:8787/npc/motion-plan
```

The earlier `ULLMNPCActionComponent` and raw motion-plan endpoint are still present as a compatibility/prototyping layer, but the primary path is now Dialogue plus Published Templates.

The plugin ships its default post-process AnimBP, Manny skeleton profile, and published templates as cooked plugin content. Motion templates are also retained as JSON authoring sources under `Resources/Templates`.

The Phase 2 structured response schema and DeepSeek command prompt live under `Resources/Schemas` and `Resources/PromptTemplates`.
