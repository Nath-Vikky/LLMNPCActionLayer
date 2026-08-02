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
  -> restricted FLLMNPCBehaviorPlan
  -> registered scene target + AI MoveTo + target facing
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
- `FLLMNPCBehaviorPlanValidator`
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
- `FLLMNPCMotionPrimitiveRegistry`
- `FLLMNPCMotionRecipeParser`
- `FLLMNPCMotionRecipeValidator`
- `FLLMNPCMotionRecipeCompiler`
- `ULLMNPCTemplateLibrarySubsystem`
- `ULLMNPCMotionTemplate`
- `ULLMNPCSkeletonProfile`
- `ULLMNPCSkeletonProfileAuthoringSubsystem` (Editor only)
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
right_shoulder.pitch
right_shoulder.yaw
right_shoulder.roll
left_shoulder.pitch
left_shoulder.yaw
left_shoulder.roll
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
right_fingers.contact
right_fingers.relaxed
right_fingers.point
left_hand.ik
left_hand.local_offset.x
left_hand.local_offset.y
left_hand.local_offset.z
left_hand.palm_target
left_fingers.open
left_fingers.contact
left_fingers.relaxed
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
gesture.clap.manny.asset.v1
```

Runtime model selection exposes skeleton-independent public IDs such as
`gesture.nod`, `gesture.wave.right`, `gesture.point.target`, and
`gesture.clap`. The faithful FK wave remains available by exact ID for trusted
debug and review workflows, but is hidden from model candidate selection.
Target Point is offered only while at least one legal scene target exists.

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
The reviewed FK reconstruction backs both the default and `subtle` Wave; the
older hand-anchor experiment is internal-only. When `right_hand_busy` is active
and `left_hand_busy` is not, UE reflects the reviewed right-arm FK chain across
Manny's skeletal X axis and executes it on the left. Mirroring never exposes
bones, transforms, or axis rules to the model.

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
- `CancelActiveBehavior`
- `ResetConversation`
- `SetProviderKind`
- `SetMotionComponent`
- `RegisterTarget`
- `RegisterSceneTarget`
- `SetSceneStateActive`
- `CreateChatWidget`
- `GetDebugState`
- `GetBehaviorDebugState`
- `GetSelectionContextSnapshot`
- `GetLastOfferedCandidates`

Add `LLM NPC Motion Component` and `LLM NPC Dialogue Component` to the NPC actor. Set the Dialogue provider to `Mock` for a fully local first test. Enable `Auto Create Chat Widget`, or call `CreateChatWidget` from the player-facing gameplay layer. The shipped `WBP_LLMNPCChat` derives from the native widget and needs no Blueprint graph.

The local Mock recognizes English and Chinese greeting, agreement, direction, and explicit movement intents. It reads the same filtered candidate and scene-target lists as a remote model: a friendly greeting selects Wave, an agreement question selects Nod, a direction question selects Gaze + Point only when a legal Target is available, and an explicit movement request may select `move_to` only for a registered scene target. A busy right hand removes right-arm actions and falls back to Nod. Multi-turn history is bounded and only public Published candidates and opaque Target Refs are sent to a provider.

## Contextual Selection

Add optional `LLM NPC Emotion`, `LLM NPC Relationship`, and `LLM NPC Scene Context` components beside the Dialogue component. Assign an `LLMNPCPersonalityProfile` to the Dialogue component. Without them, neutral defaults are used.

`RegisterSceneTarget` registers the same opaque Target Ref with both the motion executor and scene-selection context. `SetSceneStateActive("right_hand_busy", true)` removes templates blocked by that state before the request reaches the model. Template cooldown and short-term repeat suppression also operate before selection; the motion scheduler remains the final execution-time guard.

The current request contract is `llmnpc.turn_request.v3`, with prompt version `llmnpc.selection_prompt.v3`. Providers that explicitly declare only v2 support receive a fail-closed v2 projection through `FLLMNPCTurnRequestV3Adapter`; the projection never adds candidates, targets, or private implementation fields. Context-constrained amplitude, speed, duration, style, mirror recommendation, and Target refs are enforced again after parsing. Selection events are retained in a bounded `ULLMNPCSelectionAnalyticsSubsystem` ring buffer and do not leave the process automatically.

## Restricted Compound Behavior

Phase 6 accepts one locomotion decision, `move_to`, in addition to the existing
Published template decision. UE converts the validated model turn into a small
immutable plan made only from `MoveToTarget`, `FaceTarget`, `PlayTemplate`, and
internal `Wait` steps. Runtime models cannot provide paths, coordinates,
rotations, controller classes, or animation assets.

The locomotion target must resolve through `ULLMNPCSceneContextComponent`.
Acceptance radius, movement timeout, total timeout, facing speed, facing
tolerance, and optional default AI-controller spawning are project settings.
The coordinator rejects self-targets, unavailable targets, player-controlled
owners, missing path following, partial-path failure, timeouts, and a second
plan while one is active. `GetBehaviorDebugState` exposes the current step and
stable failure code for Blueprint, PIE automation, and debugging.

## Multi-Skeleton Profiles

Phase 7 removes Manny bone names from the active procedural execution path.
`ULLMNPCMotionComponent` resolves the selected `ULLMNPCSkeletonProfile` into an
animation-thread-safe binding snapshot containing the head, chest, both arm
chains, finger chains, optional axis bases, and calibrated open/point finger
poses. `FAnimNode_LLMProceduralPose` consumes those bindings and propagates hand
motion through the actual descendant hierarchy instead of a Manny-only list.

Templates retain one primary `SkeletonProfileId` and may list explicitly
reviewed `CompatibleSkeletonProfileIds`. Exact-profile variants win over
compatible variants. This matrix never means that arbitrary skeletons are
accepted: the target Profile must exist, validate, match the active mesh
Skeleton signature, and be named by the template.

Manny and Quinn use the same UE5 Mannequin Skeleton in the demo project, so
both correctly reuse `ue5_manny.v1`; a duplicate Quinn Profile would add no
runtime distinction. Skeletons with different bone names or reference poses
need their own Profile.

The editor-only `ULLMNPCSkeletonProfileAuthoringSubsystem` can generate a
Profile from common UE or Mixamo humanoid naming, refresh generated mappings,
set an orthonormal per-bone axis calibration, and write a quality report to:

```text
Saved/LLMNPCActionLayer/Reports/SkeletonProfiles
```

Runtime axis remapping is opt-in per Profile so the already approved Manny
axis convention remains unchanged. Generated Profiles include default finger
pose calibration, arm IK chains, a Skeleton signature, and coverage metrics;
they still require preview and human approval before templates declare them
compatible.

## Model Providers

- `Mock`: deterministic, local, and the default.
- `BackendProxy`: recommended runtime path; POSTs the turn context to `BackendProxyEndpoint`.
- `DeepSeekDirectEditorOnly`: development-only direct adapter. It is disabled unless explicitly enabled in Project Settings. The editor can import its model, endpoint, and session credential from the project-root `env.txt`; a named process environment variable remains a fallback credential source.

The backend may return either a `llmnpc.model_turn.v1` object or `{ "decision": <model-turn-object> }`. The model response is parsed with unknown-field rejection and then checked against the local Published template library. Invalid action IDs preserve displayable assistant text but cannot drive the body. Direct provider credentials are compiled out of non-editor targets, and runtime builds are expected to use a trusted backend proxy.

Default dialogue backend endpoint:

```text
http://localhost:8787/v1/npc/turn
```

External runtime modules can register additional factories through
`FLLMNPCModelProviderRegistry` and select them with `ProviderIdOverride`,
`SetProviderId`, or project-level `DefaultProviderId`. Unknown provider IDs fail
closed. A request watchdog recovers providers that never invoke their callback,
and the local fallback is attempted at most once.

## Forward N0 Test Workbench

The editor module loads the following strict UTF-8 `KEY=VALUE` contract from
`FPaths::ProjectDir()/env.txt` into memory only:

```text
LLM_MODEL=<model-id>
OPENAI_BASE_URL=<openai-compatible-base-url>
OPENAI_API_KEY=<session-credential>
```

Unknown, duplicate, missing, empty, or malformed entries fail the online gate.
The API key is injected into the existing editor Session Secret store and is
never copied into Settings, reports, assets, or logs. Model and endpoint
overrides are cleared on editor shutdown. The UI displays only model ID,
endpoint origin, a non-secret config hash, and whether a credential is present.

Open `Tools > LLM NPC Provider Settings`, select
`DeepSeek Direct (Editor)`, enable direct editor access, and run `Test DeepSeek`.
The strict connection gate verifies the actual provider ID, returned model ID,
`llmnpc.model_turn.v1` response schema, HTTP result, and the frozen config hash.
A successful HTTP response with a different provider/model or stale config does
not pass.

Open `Tools > LLM NPC Motion Test Console` during PIE to:

- enumerate every Published template compatible with the selected NPC profile;
- execute custom or minimum/default/maximum modifiers;
- run a repeatable three-point parameter sweep;
- inspect requested/resolved modifiers, channels, target, validator, active
  pose, provider, candidates, behavior, fallback, and IK state;
- run a strict natural-language online selection with Mock fallback disabled;
- save an allowlisted, sanitized report under
  `Saved/LLMNPCActionLayer/ForwardN0/Reports`.

Online reports store an input hash rather than the natural-language payload.
Authorization headers, API keys, credentials, request/response headers, and raw
provider payloads are removed by the final report sanitizer. Visual quality in
PIE remains a human review decision and is written as `pending` by default.

## Forward N1 Capability And Constraints

The shipped `ue5_manny.v1` Profile now exposes a versioned, model-safe
Capability Snapshot for the allowed upper-body social-motion domain. The model
sees semantic abilities, supported sides, target modes, dependencies, and
normalized parameter ranges. It does not receive actual bone names, Compact
Pose indices, axis bases, transforms, quaternions, IK implementation IDs, or
component-space data.

Version-controlled N1 artifacts:

```text
Resources/Schemas/llmnpc_skeleton_capability_v1.schema.json
Resources/Capabilities/Manny/ue5_manny_v1.capability.json
Resources/Validation/Manny/MannyValidationBaseline.v1.json
Content/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.uasset
```

The Manny Profile includes shoulder bindings, calibrated Open/Point/Relaxed/Curl
finger poses, per-control velocity/acceleration/jerk limits, IK reach bounds,
upper-body collision proxies, and stable ground-contact markers. Relaxed/Curl
calibration uses a revisioned one-time migration so future Profile refreshes
preserve deliberate project calibration.

`FLLMNPCKinematicValidator` validates joint ranges, angular/positional/
normalized derivatives, IK reach, finite samples, and start/end continuity.
`FLLMNPCPoseOutputContract` removes invalid or duplicate transforms and sorts
the final Compact Pose buffer before animation-thread output. Behavior-affecting
constraints invalidate the Capability Hash; approval names, dates, and
Baseline-management state do not.

Editor commands:

```text
LLMNPC.ExportMannyCapability
LLMNPC.RefreshMannyN1Profile
LLMNPC.ApproveMannyN1ValidationBaseline
LLMNPC.RunMannyN1CapabilitySmoke
```

`ExportMannyCapability` regenerates only the model-safe JSON artifact and does
not modify the Manny Profile or its approved validation baseline.

`ApproveMannyN1ValidationBaseline` is an explicit human-gated operation. It
derives per-control thresholds from all Published Manny procedural templates,
adds 20 percent headroom, writes the values into the Profile, requires every
template diagnostic to pass, pins the resulting Baseline Hash, and writes the
approver/date record. It must only run after Manny PIE review.

The Motion Test Console contains a `Forward N1 Pose Review` section for isolated
shoulder, Relaxed-hand, and Curl-hand checks. The raised arm in the hand tests
is a shared inspection pose, not a Published gesture. PIE visual acceptance
remains human-owned.

The strict online Capability Smoke uses the editor-only provider configured by
`env.txt`, disables Mock fallback, checks exact semantic capability selection,
and saves only sanitized evidence under:

```text
Saved/LLMNPCActionLayer/ForwardN1/Reports
```

See `Docs/Phases/forward-n1-capability-constraints.md` for the frozen N1
acceptance record.

## Forward N2 Catalog And Workbench

N2 separates model-facing Public Actions from skeleton-specific runtime
templates. Published Nod, Wave, and Point definitions describe intent, suitable
and unsuitable contexts, target contracts, semantic effects, and allowed
styles. Their Manny variants retain the private execution details. The runtime
search index offers only the latest valid Published definitions and variants;
Generated or invalid assets never enter normal provider candidates.

Open the unified editor surface from `Tools > LLM NPC Template Workbench`, or
run:

```text
LLMNPC.OpenTemplateWorkbench
```

Its Library, Preview, Quality, and Review pages provide catalog search, exact
Turn Request v3 Candidate Card inspection, deterministic diagnostics, human
review, and explicit publication to project-owned Published paths. Model-facing
descriptions and valid vocabulary tags are mandatory publication gates.

Version-controlled N2 artifacts include:

```text
Resources/Schemas/llmnpc_turn_request_v3.schema.json
Resources/Schemas/llmnpc_public_action_definition_v1.schema.json
Resources/Schemas/llmnpc_action_vocabulary_v1.schema.json
Resources/Vocabulary/action_vocabulary_v1.json
Resources/Catalog/Manny/public_actions_v1.json
Content/LLMNPC/PublicActions
Content/LLMNPC/Catalog
```

Use `LLMNPC.MigrateMannyN2Catalog` for the idempotent Manny migration and
`LLMNPC.RunMannyN2CatalogSelectionSmoke` for the strict real-provider selection
suite. See `Docs/Phases/forward-n2-catalog-workbench.md` for the frozen N2
acceptance record.

## Forward N4 Animation Asset Publication

N4 adds a strict Animation Asset Draft importer and the first complete
Workbench publication loop for `gesture.clap`. A model can describe the action
and bounded playback policy, but cannot provide an Unreal asset path. The user
selects a saved compatible Animation Asset in the UE Asset Picker, then the
Workbench validates its package fingerprint, skeleton, slot, Root Motion
policy, provenance, quality report, and human review before publication.

For an editable project plugin, approved canonical JSON is versioned under:

```text
Resources/Templates/Published
Resources/PublicActions/Published
```

Installed plugins fall back to
`<Project>/LLMNPCSource/Templates/Published` and
`<Project>/LLMNPCSource/PublicActions/Published`, so project authors never
need to write into a read-only plugin installation. Runtime still loads cooked
Data Assets rather than JSON. Licensed raw Animation Assets marked as
non-redistributable remain host-project dependencies and are not copied into
the plugin.

See `Docs/Phases/forward-n4-animation-asset-clap.md` for the accepted online PIE
evidence and release boundary.

## Forward N5 Motion Recipe Authoring

N5 adds a no-reference procedural authoring path. The online Authoring Model
receives a capability-filtered semantic Primitive Registry, a strict Motion
Recipe Schema, the desired action, and bounded Published-template summaries.
It returns only registered primitives and bounded semantic parameters. Bone
names, transforms, axes, IK anchors, control IDs, and solver IDs remain private
to Unreal.

The first Recipe compiler target is a Manny bilateral Shrug with coordinated
shoulders, upper chest, arm IK, wrists, and calibrated relaxed fingers. N7-B
adds the second Authoring Contract, a bilateral Procedural Clap driven by the
semantic `hands.contact` primitive.
Generated Recipes are deterministically compiled into quarantined Motion
Template Drafts. They must pass evidence-bound quality checks, human PIE
preview, review, and explicit publication before runtime selection can see
them.

Version-controlled N5 artifacts include:

```text
Resources/Schemas/llmnpc_motion_recipe_v1.schema.json
Source/LLMNPCActionLayer/Public/MotionRecipe
Source/LLMNPCActionLayer/Private/MotionRecipe
```

Open `Generate` in the Template Workbench and select a `Recipe Contract` for
the online authoring flow. The
checked-in Schema can be refreshed with
`LLMNPC.ExportMotionRecipeSchema`. See
`Docs/Phases/forward-n5-motion-recipe-shrug.md` for trust boundaries,
provenance, quality gates, and acceptance steps.

## Forward N7 Rejected Draft Revision

The first N7 gate closes the online model-derived action loop without editing
or overwriting a failed Draft. On the Workbench `Review` page, `Revise Online`
records reviewer identity and visual feedback, rejects the source Motion Recipe
Draft, and prepares a new online request. The request and Authoring Job bind
`RegenerateRejectedDraft` to the parent Template ID and Recipe hash. UE rejects
the revision if the parent is missing, changed, not Rejected, uses another
Manny profile, or belongs to another Public Action.

The revision receives a new Template ID and must repeat Sandbox visual review,
Quality, Previewed, HumanApproved, and Publish. Existing prompt-v2 Drafts
remain intentionally stale. Historical prompt-v3 provenance remains readable;
new generation and revision requests use prompt v4 with an immutable Authoring
Contract ID.

See `Docs/Phases/forward-n7a-rejected-draft-revision.md` for the exact gate and
manual online acceptance sequence.

## Forward N7-B Procedural Clap

N7-B adds Authoring-only `hands.contact` for Manny. The online model chooses
clap count, amplitude, speed, height, separation, and palm openness; Unreal
generates mirrored arm IK, contact/release keyframes, shared palm facing, open
fingers, and recovery. Either occupied hand or an active two-hand interaction
blocks the action.

The existing Published Mixamo `Clapping` template remains the AnimationAsset
baseline under `gesture.clap`. A generated procedural Clap becomes another
variant of that Public Action only after Sandbox visual review, Quality,
HumanApproved, and Publish.

See `Docs/Phases/forward-n7b-procedural-clap.md` for protocol versions,
calibration evidence, automated coverage, and the PIE comparison gate.

## Forward N7-C Procedural Beckon

N7-C adds Authoring-only `hand.beckon` for Manny. The online model chooses a
side, semantic `primary` target slot, cycle count, reach, height, amplitude,
speed, and curl amount. Unreal resolves the target and generates constrained
arm IK, palm orientation, complementary Relaxed/Curl finger curves, target-loss
fade, interruption, and recovery.

The contract never exposes concrete Actors, coordinates, bones, or wrist
rotations. The reviewed N7-C result completed human PIE review, Quality,
Previewed, HumanApproved, and explicit Publish. Runtime selection now exposes
Public Action `gesture.beckon` and resolves it to the Manny template
`gesture.beckon.manny.procedural.generated.145355d29de7`.

See `Docs/Phases/forward-n7c-procedural-beckon.md` for the target contract,
dynamic-target policy, automated coverage, and manual acceptance matrix.

## Forward N7-F Online Selection Matrix

N7-F closes the Manny action-library expansion phase with a locked 17-case
real-provider PIE matrix. It covers all eight Published Public Actions plus
`None`, targets, hand occupation, mirroring, style variants, walking, and
repeat suppression. Selection must pass without local fallback, and action
cases are not advanced until runtime playback and recovery have completed.

The accepted run passed 17 of 17 machine cases, observed and completed all 14
required playbacks with no timeout, and received explicit human visual approval
for all 17 cases. See `Docs/Phases/forward-n7f-online-selection-matrix.md` for
the locked contract, report location, and acceptance evidence.

## Product Runtime

When the owning Actor replicates, `ULLMNPCMotionComponent` replicates only a
validated high-level motion plan or Published animation template command.
Target refs resolve to replicated Actors and synchronized server time is used
to compensate late delivery. Bone transforms, evaluated pose snapshots,
prompts, and credentials never cross the network. Replicated NPC submissions
are authority-only by default.

Automatic Motion LOD samples nearby NPCs every tick, mid-distance NPCs at about
15 Hz, and distant NPCs at 4 Hz without micro motion. Dedicated servers retain
the scheduler and command replication but skip post-process animation and pose
evaluation. Use `STATGROUP_LLMNPCActionLayer` in Unreal Insights or stat tools
to inspect component and sampling cost.

Protocol versions are centralized in `FLLMNPCProtocolCompatibility`. Known
MotionPlan 1.x spellings migrate to `1.0`; Turn Request v3 can be projected to
v2 only for providers that explicitly advertise v2 support. Unknown Prompt,
response Schema, request Schema, and MotionPlan versions fail closed. Selection
telemetry is local and opt-in.

The plugin is validated with isolated Win64 `BuildPlugin` Development and
Shipping targets. See `Docs/Phases/phase8-productization.md` for networking,
Provider extension, LOD, recovery, privacy, and packaging details.

## Legacy Motion-Plan Endpoint

```text
http://localhost:8787/npc/motion-plan
```

The earlier `ULLMNPCActionComponent` and raw motion-plan endpoint are still present as a compatibility/prototyping layer, but the primary path is now Dialogue plus Published Templates.

The plugin ships its default post-process AnimBP, Manny skeleton profile, and
self-contained built-in templates as cooked plugin content. Motion templates
are also retained as JSON authoring sources under `Resources/Templates`.
Project extension templates can reference licensed host-project Animation
Assets without redistributing those raw assets through the plugin.

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
