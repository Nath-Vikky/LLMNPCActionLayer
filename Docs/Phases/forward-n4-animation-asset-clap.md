# Forward N4: Animation Asset Clap

## Status

- Release: `v0.10.0-beta.2`
- Primary skeleton profile: `ue5_manny.v1`
- Published template: `gesture.clap.manny.asset.v1`
- Public action: `gesture.clap`
- Workbench review and human PIE visual gate: passed on 2026-07-26
- Strict online evaluation: passed on 2026-07-26
- Automated regression: 83/83 passed
- Multiplayer scope: deferred

## Goal

Phase N4 establishes the first reviewed Animation Asset production loop for
Manny. A real Manny-compatible Clap Animation Sequence is selected inside
Unreal Editor, wrapped in a controlled Motion Template, reviewed, published,
offered to the online model as `gesture.clap`, and executed through Dynamic
Montage.

This phase does not generate a procedural Clap. That work remains blocked on
the future `hands.contact` primitive and bilateral contact solver.

## Trust Boundary

- The model-facing schema is `llmnpc.animation_template_draft.v1`.
- The Draft JSON cannot contain an Animation Asset path.
- The source asset is selected with the Workbench UE Asset Picker.
- The importer injects the trusted soft object path, class, skeleton, and saved
  package fingerprint into provenance.
- A dirty or unsaved source Animation Asset is rejected.
- A reimported or edited source package invalidates the old quality report,
  even when its path, skeleton, and duration remain unchanged.
- Imported templates always start as `Generated`.
- Generated, Previewed, HumanApproved, Rejected, and Draft assets are excluded
  from normal runtime model candidates.

## Workbench Flow

1. Open `Tools > LLMNPC Template Workbench`.
2. Open the `Import` page.
3. Select a saved Manny-compatible Clap Animation Sequence.
4. Review the selected asset path, class, skeleton, duration, and Root Motion
   summary.
5. Use
   `Resources/AuthoringExamples/DT_Clap_Manny_AnimationAsset_v1.json` as the
   initial Draft and update its license/provenance fields for the real source.
6. Import into `/Game/LLMNPCActionLayer/Authoring/Drafts`.
7. On `Quality`, generate a report. UEPI Reconstruction evidence is optional;
   when the Draft pins a reconstruction hash, the matching artifact becomes
   mandatory.
8. Start PIE, choose the NPC preview actor, and preview the Generated template.
9. Verify both hands, palms, all fingers, contact timing, separation, blend in,
   blend out, interruption, and base-pose recovery.
10. Enter preview notes and select `Mark Previewed`.
11. Enter reviewer identity and notes, then select `Approve`.
12. Publish to the configured Published template path.

## Fixed Asset Policy

The first Clap wrapper permits only bounded playback speed and duration:

- amplitude is fixed at `1.0`;
- mirroring is disabled;
- dynamic target tracking is disabled;
- obstacle adaptation is disabled;
- procedural amplitude, frequency, and phase jitter are disabled;
- Root Motion is disabled;
- the current Manny `DefaultSlot` is a full-pose path, so this template must
  reserve only the `full_body` scheduler channel;
- a loop must be interruptible;
- required body-region channels must be explicit and non-ambiguous.

## Quality Gate

The report must pass all applicable checks:

- template structure;
- selected Skeleton Profile;
- source asset load and supported type;
- saved package fingerprint and imported-source identity;
- Manny skeleton compatibility;
- registered Slot;
- Root Motion policy;
- fixed Animation Asset modifier surface;
- maximum duration at the slowest allowed play rate;
- loop and interruption policy;
- body-region channel contract;
- provenance JSON;
- optional UEPI evidence identity when supplied.

The report records source asset path, skeleton, play length, and package hash.
Any template edit or source Animation Asset change makes the report stale.

## Runtime Acceptance

After publication, run at least three real online turns with the configured
provider:

1. Direct: ask the NPC to clap or applaud.
2. Contextual: express success or celebration without naming the animation.
3. Negative control: use neutral dialogue where clapping is inappropriate.

For accepted Clap selections, confirm the decision uses `gesture.clap`, resolves
to the Published Animation Asset variant, and reaches the Dynamic Montage
player. For every run, a human verifies contact readability, palms, fingers,
blend, interruption, and recovery. The negative control should select another
appropriate action or `None`.

## Automation

- `LLMNPCActionLayer.ForwardN4.Authoring.StrictAnimationDraftParser`
- `LLMNPCActionLayer.ForwardN4.Authoring.AnimationDraftAssetImport`
- `LLMNPCActionLayer.ForwardN4.Authoring.PublishedSourceLivesInPlugin`
- `LLMNPCActionLayer.ForwardN4.Catalog.ClapSeedRequiresPublishedVariant`
- Existing Phase 6 Animation Asset playback and interruption tests
- Full `LLMNPCActionLayer` regression suite

## Current Gate

The Manny-compatible source exists at
`/Game/LLMNPC/Animation/Clapping.Clapping`. It is a 1.166667-second, 30 FPS,
Root-Motion-free Adobe Mixamo sequence. The raw Animation Asset remains in the
host project because its provenance forbids raw redistribution through the
plugin repository.

The Workbench flow completed on 2026-07-26. Human review accepted arms, palms,
fingers, hand contact, separation, blend, interruption, and idle recovery. The
Published template is `gesture.clap.manny.asset.v1`, exposed to the model as
`gesture.clap`. Its canonical source is versioned inside
`Resources/Templates/Published/`; the runtime UAsset and licensed source
Animation Asset remain project extensions.

The strict online PIE report
`motion_test_20260726_085951.json` records two successful Clap selections and a
negative control:

- direct request: `gesture.clap` -> `gesture.clap.manny.asset.v1`;
- contextual celebration: `gesture.clap` ->
  `gesture.clap.manny.asset.v1`;
- neutral acknowledgement: `gesture.nod` -> `gesture.nod.manny.v1`.

All accepted turns used `deepseek_direct_editor`, had no local fallback or
error, passed runtime validation, and were visually accepted by the user.
