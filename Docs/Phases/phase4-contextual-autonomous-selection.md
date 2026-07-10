# Phase 4: Contextual Autonomous Selection

## Release

- Plugin version: `0.6.0`
- Engine: Unreal Engine 5.3.2
- Runtime request schema: `llmnpc.turn_request.v2`
- Prompt contract: `llmnpc.selection_prompt.v1`
- Automation filter: `LLMNPCActionLayer.Phase4`

## Delivered

- `ULLMNPCEmotionComponent` with semantic emotion, intensity, valence, arousal,
  and runtime decay.
- `ULLMNPCPersonalityProfile` with expressiveness, shyness, sociability, and tags.
- `ULLMNPCRelationshipComponent` with familiarity, trust, affinity, and tags.
- `ULLMNPCSceneContextComponent` with opaque legal Target refs, categories,
  semantic tags, salience, availability, and active NPC states.
- `ULLMNPCCandidateRetriever` with intent ranking, Target filtering, blocked-state
  filtering, template cooldown, repeat suppression, and a bounded candidate list.
- Contextual modifier policy that narrows amplitude for personality and clamps the
  model response again before behavior execution.
- Structured action history using public selection IDs. Internal template IDs
  remain available to local debugging but are not sent to a provider.
- Versioned prompt contract and a strict JSON Schema for the v2 request.
- Bounded in-process `ULLMNPCSelectionAnalyticsSubsystem` events.
- Published `gesture.point.target.manny.v1`, combining Gaze, right-arm IK,
  palm orientation, and procedural point-hand pose.

## Runtime Selection Flow

```text
Dialogue message
  -> snapshot emotion / personality / relationship / scene
  -> query Published public candidates
  -> filter missing Target / blocked state / cooldown / repetition
  -> rank and narrow contextual modifiers
  -> provider receives llmnpc.turn_request.v2
  -> strict llmnpc.model_turn.v1 parse
  -> verify action was offered
  -> verify Target was offered
  -> clamp contextual modifiers
  -> existing template validator and motion scheduler
  -> Selection Analytics outcome
```

## Blueprint Setup

The existing Motion and Dialogue components continue to work with neutral
defaults. For contextual selection, add any of these sibling actor components:

- `LLM NPC Emotion Component`
- `LLM NPC Relationship Component`
- `LLM NPC Scene Context Component`

Create an `LLM NPC Personality Profile` Data Asset and assign it to the Dialogue
component when personality should affect expression.

Register gameplay targets through `Register Scene Target` on the Dialogue
component. This registers the opaque ref with both selection and execution:

```text
Target Ref: door.main
Category: door
Semantic Tags: door, exit
Salience: 0.9
```

Use `Set Scene State Active` for state-driven exclusions. The built-in right-arm
templates declare `right_hand_busy` as a blocked state.

## Acceptance Results

- Friendly `hello` / Chinese greeting -> `gesture.wave.right`.
- Agreement question -> `gesture.nod`.
- Door location question with legal `door.main` -> `gesture.point.target` with
  Target Ref `door.main`.
- Fully shy personality -> a lower allowed Wave amplitude, enforced in UE.
- `right_hand_busy` -> Wave and Point are removed; greeting falls back to Nod.
- Missing Target -> Point is not offered.
- Unknown/unoffered action ID -> rejected before behavior execution.
- Target Ref outside the offered candidate policy -> rejected.

## Security Boundaries

- Providers receive public action IDs and opaque Target refs, never Actors,
  bones, controls, transforms, tracks, or asset paths.
- Metadata used for local channel/state filtering is not serialized as raw
  control authority.
- Personality changes allowed modifiers; it cannot widen template policy.
- Candidate filtering is advisory only in neither direction: the same offered
  set and modifier policy are enforced after model parsing.
- The existing template compiler, motion validator, target lookup, cooldown,
  channel scheduler, animation graph, and physics remain final authorities.
- Selection Analytics is bounded, local, and has no automatic network export.

## Verification

- UE 5.3.2 Editor target builds after a clean UBT gather.
- Phase 4 automation: 6/6 passed.
- Phase 0-4 regression automation: 28/28 passed.
- Game build, Windows cook, and runtime map smoke results are recorded in the
  release commit verification.
