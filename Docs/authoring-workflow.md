# Authoring Workflow

This workflow runs only in the Unreal Editor. The runtime module does not
depend on UEProjectIntelligence, Python, Codex, or the importer.

## 1. Export Animation Evidence

Use UEProjectIntelligence to generate a
`uepi.animation_reconstruction_profile.v1` artifact for the reference Animation
Sequence. Generate or retain the Full Pose Artifact when high-fidelity source
identity and pose sampling need to be checked.

The Reconstruction Profile is the normal authoring input. Full Pose data is not
copied into a prompt, Draft, template asset, or runtime candidate.

## 2. Build Authoring Context

In the Unreal Python console:

```python
import unreal

authoring = unreal.get_editor_subsystem(
    unreal.LLMNPCTemplateAuthoringSubsystem
)
result = authoring.build_authoring_context_from_uepi_profile(
    "F:/Project/Saved/UEProjectIntelligence/store/artifacts/animation_reconstruction/<id>.json",
    "",
)
print(result.output_path)
```

The adapter validates source state, duration, driver count, key count, ordered
normalized times, and local-transform presence. It emits only bounded
Reconstruction Profile evidence plus a Full Pose reference.

## 3. Generate a Draft

Give the resulting `llmnpc.authoring_context.v1`, target Skeleton Profile, and
`Resources/PromptTemplates/codex_authoring_workflow.md` to Codex or another
development agent. The output must be one
`llmnpc.motion_template_draft.v1` object.

Bone names in the authoring context are evidence. Draft `control_id` values
must come from the plugin control manifest. The Draft cannot claim to be
Previewed, HumanApproved, or Published.

## 4. Import

```python
result = authoring.import_draft_from_file(
    "F:/Project/Saved/LLMNPCActionLayer/Authoring/Drafts/my_draft.json",
    "/Game/LLMNPCActionLayer/Authoring/Drafts",
)
template = result.template_asset
```

The original JSON is copied into the Saved Draft directory with a content hash.
The imported asset state is always `Generated`, even when the input attempts to
declare another allowed pre-review state.

## 5. Generate Quality Report

```python
quality = authoring.generate_quality_report(
    template,
    "F:/Project/Saved/UEProjectIntelligence/store/artifacts/animation_reconstruction/<id>.json",
    "F:/Project/Saved/UEProjectIntelligence/store/artifacts/animation_full_pose_samples/<id>.json",
)
print(quality.message)
```

The Full Pose path is optional. When supplied, its source sequence, skeleton,
sample count, ordered sample times, non-empty bone samples, and content hash are
verified. A report can pass without loading Full Pose only when the
Reconstruction Profile itself passes; the report records `manifest_only` and a
warning.

## 6. Preview in PIE

Place `LLMNPCTemplatePreviewActor` in a development map, assign a compatible
Skeletal Mesh and the Generated template, then start PIE. Its Motion Component
submits a transient Published copy only for preview; the source asset state is
not elevated.

After visually checking silhouette, timing, hand path, fingers, blending, and
return pose:

```python
authoring.mark_template_previewed(
    template,
    "Checked in PIE against the reference animation.",
)
```

## 7. Human Review

```python
authoring.approve_template(
    template,
    "reviewer-name",
    "Approved after visual comparison and source-license review.",
)
```

Approval requires a current passing report and Previewed state. Editing motion,
metadata, provenance, license, or artifact hashes after the report makes it
stale and closes the Publish gate.

Reject unsuitable work with `reject_template`; the Draft source is copied to
the Rejected directory when an import record is available.

## 8. Publish

```python
published = authoring.publish_template(
    template,
    "/Game/LLMNPCActionLayer/MotionTemplates",
)
print(published.output_path)
```

Publish creates a separate asset and changes only that copy to `Published`.
It rejects missing approval, stale reports, missing license data, incomplete
review records, duplicate Template IDs, invalid destination paths, and asset
name collisions. Use a new semantic/template version instead of replacing an
existing Published ID.
