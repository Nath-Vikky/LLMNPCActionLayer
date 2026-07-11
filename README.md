# LLM NPC Action Layer

Data-driven LLM procedural NPC gesture layer for Unreal Engine 5.3.

The plugin keeps large language models away from raw skeletal control. Runtime models return dialogue text plus a published action ID and constrained modifiers. Unreal Engine resolves the skeleton-specific template, validates it, schedules its channels, samples motion tracks, and applies the procedural pose through a post-process animation path.

## Runtime Flow

```text
player text / story command
  -> ULLMNPCDialogueComponent
  -> emotion / personality / relationship / scene context snapshot
  -> ULLMNPCCandidateRetriever
  -> target / occupied-state / cooldown / repetition filtering
  -> emotion / personality style resolution + deterministic seed
  -> Mock, Backend Proxy, or editor-only DeepSeek provider
  -> strict llmnpc.model_turn.v1 parser
  -> offered-candidate and contextual modifier policy
  -> deterministic Published template variant
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
- `ULLMNPCEmotionComponent`
- `ULLMNPCPersonalityProfile`
- `ULLMNPCRelationshipComponent`
- `ULLMNPCSceneContextComponent`
- `ULLMNPCCandidateRetriever`
- `ULLMNPCSelectionAnalyticsSubsystem`
- `ULLMNPCStyleResolver`
- `FLLMNPCMicroMotionScheduler`
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
left_hand.ik
left_hand.local_offset.x
left_hand.local_offset.y
left_hand.local_offset.z
left_hand.palm_target
left_fingers.open
left_fingers.point
gaze.target
```

The direct right-arm FK controls are internal-only. Published, reviewed templates may use them; runtime model plans may not.

## Published Templates

```text
gesture.nod.manny.v1
gesture.wave.right.manny.fk.v1
gesture.wave.right.manny.procedural.v1
gesture.point.target.manny.v1
gesture.wave.right.manny.subtle.v1
```

Runtime model selection exposes the skeleton-independent public IDs `gesture.nod`, `gesture.wave.right`, and `gesture.point.target`. The faithful FK wave remains available by exact ID for trusted debug and review workflows, but is hidden from model candidate selection. Target Point is offered only while at least one legal scene target exists.

## Restricted Style And Micro Motion

Phase 5 resolves `neutral`, `friendly`, `subtle`, and `excited` through local
style presets. Emotion and personality recommend a style and bounded modifiers;
the provider still chooses only from the values exposed by Template Policy. UE
generates a deterministic action seed from session/request identity and uses it
for bounded amplitude, speed, oscillator frequency, and phase variation. The
seed and curve authority are never sent to the provider.

Multiple Published assets may share one public action ID. Variant resolution is
stable, weighted, skeleton-specific, style-aware, and reproducible from the
action seed. The shipped `subtle` Wave variant is selected for shy contexts.

Templates that explicitly allow Mirror are mirrored through semantic controls.
The procedural Wave can move to the left arm when `right_hand_busy` is active
and `left_hand_busy` is not. Mirroring never exposes bones or sign rules to the
model.

`ULLMNPCMotionComponent` also runs a low-amplitude local breathing, head sway,
occasional nod, and ambient gaze scheduler. It makes no model calls and yields
whenever formal motion owns the head, chest, or gaze channel. `SetAmbientStyle`
can change the micro-motion intensity and gaze engagement using the same style
presets.

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
- `RegisterSceneTarget`
- `SetSceneStateActive`
- `CreateChatWidget`
- `GetDebugState`
- `GetSelectionContextSnapshot`
- `GetLastOfferedCandidates`

Add `LLM NPC Motion Component` and `LLM NPC Dialogue Component` to the NPC actor. Set the Dialogue provider to `Mock` for a fully local first test. Enable `Auto Create Chat Widget`, or call `CreateChatWidget` from the player-facing gameplay layer. The shipped `WBP_LLMNPCChat` derives from the native widget and needs no Blueprint graph.

The local Mock recognizes English and Chinese greeting, agreement, and direction intents. It reads the same filtered candidate list as a remote model: a friendly greeting selects Wave, an agreement question selects Nod, and a direction question selects Gaze + Point only when a legal Target is available. A busy right hand removes right-arm actions and falls back to Nod. Multi-turn history is bounded and only public Published candidates are sent to a provider.

## Contextual Selection

Add optional `LLM NPC Emotion`, `LLM NPC Relationship`, and `LLM NPC Scene Context` components beside the Dialogue component. Assign an `LLMNPCPersonalityProfile` to the Dialogue component. Without them, neutral defaults are used.

`RegisterSceneTarget` registers the same opaque Target Ref with both the motion executor and scene-selection context. `SetSceneStateActive("right_hand_busy", true)` removes templates blocked by that state before the request reaches the model. Template cooldown and short-term repeat suppression also operate before selection; the motion scheduler remains the final execution-time guard.

The request contract is `llmnpc.turn_request.v2`, with prompt version `llmnpc.selection_prompt.v2`. Context-constrained amplitude, speed, duration, style, mirror recommendation, and Target refs are enforced again after parsing. Selection events are retained in a bounded `ULLMNPCSelectionAnalyticsSubsystem` ring buffer and do not leave the process automatically.

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

Structured response/request schemas and versioned prompts live under `Resources/Schemas` and `Resources/PromptTemplates`.

## Authoring Pipeline

Phase 3 adds a development-only `LLMNPCActionLayerEditor` module. It converts a
UEProjectIntelligence Reconstruction Profile into a bounded authoring context,
imports a strict template Draft, generates a quality report, previews the Draft
in PIE, records human review, and only then permits an explicit Publish copy.

```text
Animation Sequence
  -> UEPI Reconstruction Profile
  -> llmnpc.authoring_context.v1
  -> Codex / authoring agent
  -> llmnpc.motion_template_draft.v1
  -> Generated DataAsset
  -> quality report
  -> PIE preview
  -> HumanApproved
  -> explicit Publish
  -> /Game/LLMNPCActionLayer/MotionTemplates
```

Imported JSON can only declare `draft` or `generated`; the importer always
forces `Generated`. A passing report is bound to motion data, provenance,
license, Reconstruction Profile hash, and optional Full Pose hash. Any later
change invalidates the report. Review records do not invalidate otherwise
unchanged content. Runtime template discovery still indexes only assets whose
local state is `Published`.

Authoring files are stored under:

```text
Saved/LLMNPCActionLayer/Authoring/Contexts
Saved/LLMNPCActionLayer/Authoring/Drafts
Saved/LLMNPCActionLayer/Authoring/Reports
Saved/LLMNPCActionLayer/Authoring/Rejected
```

See `Docs/authoring-workflow.md` for the repeatable editor and Python workflow.
