# Forward N5: Motion Recipe Shrug Authoring

## Status

- Target release: `v0.10.0-beta.3`
- Primary skeleton profile: `ue5_manny.v1`
- Motion Recipe schema: `llmnpc.motion_recipe.v1`
- Primitive Registry: `llmnpc.motion_primitives.v1`
- Compiler: `llmnpc.motion_recipe_compiler.v2`
- Automated N5 gate: 10/10 passed
- Real online generation, human PIE review, publication, and online runtime
  selection: pending
- Multiplayer scope: deferred

## Goal

Phase N5 establishes the first no-reference procedural action authoring loop.
An online Authoring Model receives a model-safe Manny capability view, a strict
Motion Recipe JSON Schema, a natural-language action request, and bounded
summaries of Published templates. It returns semantic primitives and bounded
parameters. Unreal validates and deterministically compiles the Recipe into a
Generated Motion Template.

The first acceptance action is a bilateral uncertainty Shrug. It is not backed
by an Animation Sequence or a hard-coded model response.

## Trust Boundary

The model may choose:

- a registered semantic primitive;
- semantic side and validated target slot when the primitive permits them;
- start and end time;
- primitive-specific bounded parameters;
- a short catalog description for human review.

The model never receives or controls:

- bone names or compact-pose indices;
- transforms, quaternions, or axis mappings;
- control IDs, solver IDs, IK anchors, or Animation Assets;
- arbitrary Blueprint, C++, Control Rig, or console commands;
- runtime publication state.

Unknown fields, unknown primitives, unavailable capabilities, invalid sides,
invalid targets, non-interruptible Recipes, overlaps that violate ownership,
excessive duration, excessive body-region scope, and out-of-range values fail
closed.

## Protocols

The authoring request uses:

```text
llmnpc.motion_recipe_authoring_prompt.v2
llmnpc.motion_recipe_authoring_response.v1
llmnpc.motion_recipe_authoring_job.v1
```

The N5 prompt has a Shrug-specific generation contract: exactly one
`shoulder.shrug` primitive with `express_uncertainty` intent. Timing phases are
owned by Unreal's solver and may not be invented as `hold`, `ease`, `pause`,
or transition primitives. A response is shown as accepted only after it passes
the UE Recipe Parser and the current Manny Capability Validator.

It is deliberately separate from `llmnpc.model_turn.v3`. Runtime selection
chooses only Published public actions; authoring creates a quarantined
Generated Draft.

The checked-in base Schema is:

```text
Resources/Schemas/llmnpc_motion_recipe_v1.schema.json
```

Run the following editor command after changing the Primitive Registry:

```text
LLMNPC.ExportMotionRecipeSchema
```

Automation compares the checked-in file with the registry-generated Schema and
rejects implementation details such as solver IDs and Manny bone names.

## Primitive Registry

The v1 registry contains bounded semantic primitives for:

- head nod, shake, and tilt;
- gaze tracking;
- chest lean and turn;
- bilateral shoulder shrug;
- arm reach and presentation;
- hand wave arc;
- open, relaxed, curl, and point hand poses.

Each definition owns its parameter schema, allowed sides and target modes,
required semantic capabilities, blocked states, channel patterns, overlap
policy, availability, and private solver mapping. The model-facing projection
contains only the information needed to write a valid Recipe.

`shoulder.shrug` is Authoring-only in N5. Runtime never compiles an unreviewed
Recipe.

## Shrug Compiler

The Manny Shrug solver converts one validated `shoulder.shrug` primitive into
coordinated tracks for:

- calibrated left and right shoulder rotation;
- subtle upper-chest support;
- bilateral hand IK through private Shrug anchors;
- component-space palm-up presentation derived from the Manny hand geometry;
- calibrated relaxed finger poses;
- a shaped raise, readable hold, and smooth return to neutral.

The public parameter surface is:

```text
amplitude
speed
torso_participation
arm_openness
palm_openness
asymmetry
```

All values are clamped by the strict Schema and validated again before
compilation. Identical canonical Recipe and Capability inputs produce the same
compiled MotionClip and hashes.

Compiler v2 treats Manny component `X` as the bilateral mirror axis, keeps both
hands on the shared forward side, raises the clavicles with mirrored Pitch
signs, and lets arm IK continue from the already-modified shoulder pose. Relaxed
palms are oriented from the live finger and palm basis instead of hard-coded
left/right wrist Euler angles. Partial IK blends the effector target before
solving so both arm segments retain their authored length, and the mirrored
left-hand basis reverses its raw geometric normal before palm-facing alignment.
Palm alignment is decomposed into forearm-axis twist and wrist swing. The solved
upper arm, lower arm, and elbow remain unchanged; pronation or supination is
applied at the hand with anatomically bounded axial twist and wrist swing.
Authored twist and corrective bones are not rotated directly by the procedural
node. Manny or Quinn joint deformation is instead left to the skeletal mesh's
native post-process Pose Drivers, after the procedural pose has run in the main
AnimGraph. The runtime node can read its snapshot directly from the owning
actor's `LLMNPCMotionComponent`, so this integration does not require a custom
AnimInstance parent or a per-frame Blueprint copy graph.

The Manny compatibility baseline must use the full `SKM_Quinn` mesh. The
89-bone `SKM_Quinn_Simple` mesh has no native post-process Anim Blueprint, so it
cannot evaluate the Quinn elbow, forearm, wrist, or hand corrective Pose Drivers.

## Workbench Flow

1. Configure the editor-only online provider and pass `Test Connection`.
2. Open `Tools > LLM NPC Template Workbench`.
3. Open `Generate`.
4. Keep `ue5_manny.v1` selected.
5. Enter the desired action and select `Generate Online`.
6. Review the returned Recipe and sanitized Generation Evidence.
7. Optionally adjust only timing and existing bounded parameter values.
8. Select `Create Generated Draft`.
9. On `Quality`, generate a report without UEPI Reconstruction input.
10. Start PIE, select the NPC preview actor, and preview the Generated template.
11. Verify shoulders, chest, elbows, palms, fingers, symmetry, interruption,
    and return to the base pose.
12. Mark the template Previewed, record human review, and approve it.
13. Review and publish the separate Generated Public Action Definition.
14. Publish the HumanApproved Motion Template.

For an editable project plugin, both canonical Published source files are
written under `Resources/PublicActions/Published` and
`Resources/Templates/Published`. A read-only installed plugin falls back to
the project-owned `LLMNPCSource` directories.

Changing Recipe structure, primitive IDs, side, target slots, or order after
the online response requires a new online generation request. This prevents a
manually authored Recipe from inheriting online provenance.

## Evidence And Quality

The Workbench stores a sanitized Authoring Job under:

```text
Saved/LLMNPCActionLayer/Authoring/Drafts
```

The provenance envelope binds:

- provider and model identity;
- non-secret provider configuration hash;
- prompt version and prompt hash;
- Capability hash;
- Primitive Registry and Compiler versions;
- original online response;
- normalized working Recipe and Recipe hashes;
- deterministic compiled MotionClip hash;
- bounded human-edit status;
- Job file hash and generation metrics.

Credentials and authorization headers are never persisted. Quality reparses,
revalidates, and recompiles the Recipe against the current Manny Capability
and rejects stale, missing, or modified evidence. A hand-edited compiled Clip
also fails the gate.

## Acceptance

Before tagging N5:

1. Generate at least one Shrug Recipe with the real provider from `env.txt`.
2. Pass Recipe quality without an Animation Sequence or Reconstruction
   Profile.
3. Obtain human PIE acceptance for the generated Shrug.
4. Publish its Public Action Definition and Motion Template.
5. Start a new online runtime turn and confirm the model selects the Published
   Shrug from the Catalog.
6. Preserve sanitized Authoring Generation and Runtime Selection reports.

The human owns visual PIE acceptance. Automated tests prove protocol,
validation, determinism, provenance, and publication boundaries, but do not
claim that the motion looks correct.

## Automation

- `LLMNPCActionLayer.ForwardN5.Editor.AuthoringPrompt`
- `LLMNPCActionLayer.ForwardN5.Editor.RecipeDraftQuality`
- `LLMNPCActionLayer.ForwardN5.Editor.SchemaArtifact`
- `LLMNPCActionLayer.ForwardN5.Editor.Vocabulary`
- `LLMNPCActionLayer.ForwardN5.MotionRecipe.CapabilityAndPolicy`
- `LLMNPCActionLayer.ForwardN5.MotionRecipe.DeterminismAndConflicts`
- `LLMNPCActionLayer.ForwardN5.MotionRecipe.RegistrySolverCoverage`
- `LLMNPCActionLayer.ForwardN5.MotionRecipe.SafeModelSchema`
- `LLMNPCActionLayer.ForwardN5.MotionRecipe.ShrugCompiler`
- `LLMNPCActionLayer.ForwardN5.MotionRecipe.StrictParser`
