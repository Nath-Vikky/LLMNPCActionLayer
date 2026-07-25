# Forward N2: Catalog And Template Workbench

## Status

- Release: `v0.10.0-alpha.3`
- Primary skeleton profile: `ue5_manny.v1`
- Catalog schema: `llmnpc.template_catalog.v1`
- Request schema: `llmnpc.turn_request.v3`
- Prompt version: `llmnpc.selection_prompt.v3`
- Workbench visual QA: passed on 2026-07-25
- Final human Workbench gate: passed on 2026-07-26
- Multiplayer scope: deferred

## Delivered

- Catalog metadata for model-facing descriptions, semantic effects, target
  contracts, body regions, styles, tags, versions, review state, and hashes.
- Public Action Definition assets for Nod, Wave, and Point, separated from
  Manny-specific runtime variants.
- Versioned Action Vocabulary with strict tag validation.
- Deterministic catalog index, latest-version resolution, diagnostics, and
  model-safe Candidate Card construction.
- Turn Request v3 plus a fail-closed v3-to-v2 provider adapter.
- Project-owned Published destinations for templates and Public Actions.
- One Template Workbench Nomad tab with Library, Preview, Quality, and Review
  pages.
- Description and human-review publication gates.
- Sanitized real-provider selection evaluation covering action, None, target,
  missing-target, unsupported-action, and unoffered-candidate cases.

## Frozen Catalog

```text
Catalog Hash:
md5:82c3270b3bc65bf0110c45a0ffb373df

Published Public Actions: 3
Published Manny templates: 6
Catalog diagnostics: 0
```

The three Public Actions are `gesture.nod`, `gesture.wave.right`, and
`gesture.point.target`. The six Manny runtime variants include Nod, Point,
animation-asset Wave, FK Wave, procedural Wave, and subtle procedural Wave.
Every Published entry has a model-facing description and valid vocabulary
metadata.

Generated, Draft, HumanApproved, rejected, invalid, shadowed, and
skeleton-incompatible entries are excluded from normal runtime candidates.
Only opaque target refs and public semantic metadata can cross the provider
boundary. Bone names, transforms, quaternion data, asset paths, control IDs,
and unfiltered catalog entries remain private to UE.

## Workbench Verification

The Workbench was opened in the fixed Manny project and all four pages were
inspected at the native 2560 x 1440 desktop resolution:

- Library displayed 6 templates, 3 Public Actions, search, state filtering, and
  selected-asset details.
- Preview displayed the exact Turn Request v3 Candidate Card for the selected
  Public Action and `ue5_manny.v1`.
- Quality displayed the frozen Catalog Hash, description gate, validation
  result, and zero diagnostics.
- Review displayed the current review state, project Published destination,
  reviewer fields, and state-appropriate action controls.

Publication implementation is also covered by editor automation. The unified
workflow received final human acceptance on 2026-07-26.

## Automated Verification

```text
Full LLMNPCActionLayer suite: 75/75
Failures: 0
```

N2 coverage includes:

- deterministic vocabulary, Public Action, template, and catalog hashing;
- tag, description, review-state, and publication gates;
- latest-version and Generated exclusion rules;
- exact Candidate Card field allowlisting;
- Turn Request v3 construction and safe v2 projection;
- unsupported, missing-target, and unoffered-candidate rejection;
- Workbench registration and project Published paths;
- contextual style bounds across every offered style option.

## Online Verification

Provider: `deepseek_direct_editor`

Model: `deepseek-v4-flash`

The versioned suite ran seven cases three times each:

```text
Status: passed
Samples: 21/21
Schema success rate: 1.0
Illegal candidate rate: 0.0
Unnecessary action rate: 0.0
Missed action rate: 0.0
Target error rate: 0.0
Validator reject rate: 0.0
Execution completion rate: 1.0
Average latency: 2.202 seconds
P95 latency: 2.649 seconds
Average tokens: 2422.048
```

Raw requests, raw responses, credentials, and headers were not persisted. The
final secret-pattern scan found zero matches. The sanitized local report is:

```text
Saved/LLMNPCActionLayer/ForwardN2/Reports/catalog_selection_20260725_131201.json
```

`Saved` remains outside the release.

## Reproduction

Refresh the version-controlled Manny catalog:

```text
LLMNPC.MigrateMannyN2Catalog
```

Open and inspect the unified workflow:

```text
LLMNPC.OpenTemplateWorkbench
```

Then run:

```text
Automation RunTests LLMNPCActionLayer
LLMNPC.RunMannyN2CatalogSelectionSmoke
```

Any change to Public Action semantics, template catalog metadata, vocabulary,
Candidate Card construction, request adaptation, or publication rules changes
the acceptance surface and requires a new catalog hash and N2 regression run.
