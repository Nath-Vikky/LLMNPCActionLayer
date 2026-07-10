# Phase 3: UEPI/Codex Authoring Pipeline

## Release

- Plugin version: `0.5.0`
- Engine: Unreal Engine 5.3.2
- Editor module: `LLMNPCActionLayerEditor`
- Automation filter: `LLMNPCActionLayer.Phase3`

## Delivered

- Versioned Authoring Context, Motion Template Draft, and Quality Report schemas.
- Codex authoring prompt and repeatable workflow documentation.
- UEPI Reconstruction Profile adapter with bounded driver/key validation.
- Full Pose Artifact reference handling and optional deep validation.
- Strict Draft parser and DataAsset importer.
- Saved Contexts, Drafts, Reports, and Rejected directories.
- Original Draft preservation with a content hash and import record.
- Quality checks for structure, control trust, skeleton profile, provenance,
  Reconstruction Profile identity/hash, and Full Pose identity/hash.
- Content-bound reports that become stale after motion, metadata, provenance,
  license, or artifact changes.
- PIE `LLMNPCTemplatePreviewActor` and existing-actor preview API.
- Explicit Generated -> Previewed -> HumanApproved -> Published transitions.
- Reviewer identity, notes, timestamp, license, and review-history records.
- Publish collision checks and project-side Published asset directory.
- Runtime Asset Manager scan support for project-authored Published templates.

## Security Boundaries

- The Editor module is not part of Game or Shipping targets.
- UEPI is read through a JSON file protocol; there is no module dependency.
- Imported Drafts cannot self-declare HumanApproved or Published.
- Raw bone-looking control IDs fail the Published Template quality gate.
- Full Pose samples never enter runtime template data or model candidates.
- Quality success alone does not approve or publish an asset.
- Human approval alone is insufficient when the quality report is missing or stale.
- Publish creates a separate asset; the source review asset remains HumanApproved.
- Runtime discovery continues to filter on local `Published` state.

## Real Waving Verification

The project Waving artifacts were exercised end to end:

```text
Reconstruction Profile size: 4,341,633 bytes
Driver curves: 30
Driver keys: 4,290
Full Pose Artifact size: 29,106,521 bytes
Full Pose samples: 143
Bones per Full Pose sample: 161
Quality status: pass
Full Pose validation: artifact_checked
Reconstruction hash: matched
Full Pose hash: matched
```

The generated authoring context is approximately 3.16 MB and contains driver
evidence without embedding the 29 MB Full Pose sample array.

## Verification

- UE 5.3.2 clean editor target build: passed.
- Win64 Development game target build: passed without linking the Editor module.
- Phase 0 through Phase 3 automation: 22/22 passed.
- Phase 3 automation: 4/4 passed.
- Strict self-publication rejection: passed.
- Unknown control quality rejection: passed.
- Human approval state gate: passed.
- Stale report detection after motion edits: passed.
- Editor DataAsset import and cleanup: passed.
- Real Waving Reconstruction and Full Pose quality run: passed.
- Temporary Previewed -> HumanApproved -> Published copy and cleanup: passed.
- Runtime test-map startup: passed; 3 Published templates and 1 skeleton profile indexed with 0 errors.
- Windows cook: passed with 0 errors.
- Cooked output contains no `LLMNPCActionLayerEditor` module or Draft example asset.

## Manual Visual Review Still Required

The included UEPI-derived Draft is an authoring example, not a Published runtime
asset. A developer must preview it in PIE, compare it to the source Waving
sequence, record review notes, approve it, and explicitly publish a new version.
Automated evidence validation does not make a visual-quality judgment.
