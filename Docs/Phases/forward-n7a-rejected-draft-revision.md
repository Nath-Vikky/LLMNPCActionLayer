# Forward N7-A: Rejected Draft Revision And Published Reuse

## Status

- Target release: `v0.10.0-rc.2`
- Primary skeleton profile: `ue5_manny.v1`
- Authoring request: `llmnpc.motion_recipe_authoring_request.v2`
- Authoring prompt: `llmnpc.motion_recipe_authoring_prompt.v3`
- Authoring job: `llmnpc.motion_recipe_authoring_job.v2`
- Automated N7-A gate: 1/1 passed
- Full plugin release regression: 102/102 passed
- Real online regeneration, human PIE approval, and publication: passed
  2026-07-27
- Real online runtime reuse: passed 2026-07-27 with three fresh acceptance
  turns

## Goal

N7-A finishes the first model-derived action loop before the motion language is
expanded. A visually unsuitable online Shrug Draft is preserved as Rejected.
The Workbench sends its bounded human feedback to a new online request, creates
a separate Generated revision, and keeps the full parent-child lineage.

No Draft is repaired in place and no approval state is inherited.

## Revision Contract

`Revise Online` is available only for a non-Published procedural Motion Recipe
template. It records:

```text
TriggerSource = RegenerateRejectedDraft
Parent TemplateId
Parent RecipeHash
Human Review Feedback
Manny Skeleton Profile
Public Action Id
```

Before a generated response becomes a UAsset, UE verifies that:

- the parent still exists;
- the parent state is `Rejected`;
- its Recipe hash still matches the request;
- it uses the same Manny Skeleton Profile;
- it belongs to the same Public Action;
- feedback is present and bounded;
- the current Capability, Primitive Registry, Schema, Compiler, compiled plan,
  and kinematic report still match the online evidence.

The new Draft receives a new Template ID. The Authoring Job and template
provenance contain the same trigger envelope and are cross-checked during
Quality.

## Workbench Flow

1. Select the unsuitable generated Shrug in `Library`.
2. Open `Review`.
3. Enter reviewer identity.
4. Enter concrete visual feedback, including the body side and visible fault.
5. Select `Revise Online`.
6. Confirm `Request Source` shows `RegenerateRejectedDraft` and the parent ID.
7. Start PIE and select the Manny preview actor.
8. Select `Generate Online`.
9. Run `Preflight and Preview`.
10. Record `Visual Pass` only when the revision is acceptable.
11. Send it to a new Generated Draft.
12. Generate Quality, preview the Draft in PIE, mark Previewed, and approve.
13. Approve and publish the `gesture.shrug` Public Action when it is not already
    Published.
14. Publish the HumanApproved Motion Template copy.
15. Start a new real online runtime turn and verify natural uncertainty
    dialogue selects the Published Shrug.

## Online Acceptance Cases

Run at least three fresh runtime turns:

```text
I am not sure what happened.
Maybe, but I cannot promise.
Do you know which choice is correct?
```

Acceptance requires:

- the provider report proves a real online response, not Mock fallback;
- `gesture.shrug` is selected where semantically appropriate;
- the resolved Template ID is the new Published revision;
- Manny executes the same visual motion that was approved;
- no Generated or Rejected asset appears in runtime candidates;
- sanitized generation and runtime-selection reports are retained.

## Automation

```text
LLMNPCActionLayer.ForwardN7.Editor.RejectedDraftRegeneration
```

The test proves manual trigger recording, rejected-parent enforcement,
immutable child identity, trigger lineage in Job and provenance, and
deterministic Quality acceptance.

## Real Acceptance Evidence

```text
Provider: deepseek_direct_editor_authoring
Model: deepseek-v4-flash
Rejected parent: gesture.shrug.manny.generated.37eb2ce0a588
Published child: gesture.shrug.manny.generated.5704441acd3d
Public Action: gesture.shrug 1.0.0 revision 1
Quality: pass
Human PIE review: approved with minor detail polish deferred
Runtime report: motion_test_20260727_073605.json
Runtime provider: deepseek_direct_editor
Runtime model: deepseek-v4-flash
Runtime acceptance: 3/3 passed without local fallback
```

Canonical sources:

- `Resources/PublicActions/Published/gesture_shrug_1_0_0_r1.json`
- `Resources/Templates/Published/gesture_shrug_manny_generated_5704441acd3d_1_0_0_r1.json`

The published child retains the rejected-parent Template ID, Recipe hash, and
bounded human visual feedback. The source files contain no provider credential
or authorization material.

The sanitized runtime report remains outside source control at:

```text
Saved/LLMNPCActionLayer/ForwardN0/Reports/motion_test_20260727_073605.json
```

All three independent requests offered the same five Published actions,
selected `gesture.shrug`, resolved the Published child above, passed runtime
validation, started the behavior, and executed the action. Each result records
`strict_provider_identity = true` and `used_local_fallback = false`. The user
completed the corresponding PIE observation and reported no additional visual
failure. N7-A is complete.
