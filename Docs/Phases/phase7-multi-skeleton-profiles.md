# Phase 7: Multi-Skeleton Profiles

Phase 7 turns `ULLMNPCSkeletonProfile` from a submission-time compatibility
check into the source of truth used by the procedural animation runtime.

## Delivered

- Profile-driven head, chest, bilateral arm, hand, and finger bone bindings.
- Animation-thread-safe resolved bindings carried by
  `FLLMProceduralPoseSnapshot` without UObject access on worker threads.
- Hierarchy-driven hand descendant propagation for arbitrary humanoid naming.
- Optional per-profile axis remapping with additive rotation limits.
- Profile-driven `open` and `point` finger calibration.
- Primary plus explicitly compatible Skeleton Profile IDs on motion templates.
- Exact-profile-first template variant resolution.
- Core, finger-bone, finger-pose, axis, IK, and signature quality metrics.
- Editor Profile generation for common UE and Mixamo humanoid names.
- Editor axis-calibration and JSON quality-report operations.
- Manny and Quinn compatibility coverage.

## Runtime Contract

The runtime model still selects only a public action ID and constrained
modifiers. It never selects a bone, axis, Skeleton asset, or Profile.

```text
published action ID
  -> local template variant for active Profile
  -> template compatibility matrix
  -> active mesh Skeleton signature check
  -> compiled motion plan
  -> cached resolved Profile bindings
  -> procedural AnimNode
```

The template library prioritizes variants in this order:

1. Exact Profile with requested style.
2. Exact Profile generic variant.
3. Explicitly compatible Profile with requested style.
4. Explicitly compatible Profile generic variant.

Compatibility is opt-in. An empty compatibility list preserves the previous
exact-only behavior.

## Profile Generation

Use the editor subsystem `LLMNPC Skeleton Profile Authoring Subsystem` through
Blueprint, editor scripting, or a supported editor automation bridge.

`Generate Profile` requires:

- a `USkeleton`;
- a stable Profile ID such as `custom_humanoid.v1`;
- a destination package path;
- an explicit decision on runtime axis calibration.

The generator recognizes UE mannequin naming and common Mixamo naming, fills
the two arm IK chains, creates default `open` and `point` finger poses, refreshes
the Skeleton signature, saves the DataAsset, and writes a JSON report under:

```text
Saved/LLMNPCActionLayer/Reports/SkeletonProfiles
```

Axis remapping defaults to disabled. `Set Axis Calibration` accepts only a
near-orthonormal pitch/yaw/roll basis and can enable runtime remapping after a
human preview confirms the result.

Existing Profiles can be refreshed without opening an interactive editor:

```text
UnrealEditor-Cmd.exe <Project>.uproject -run=LLMNPCSkeletonProfile \
  -Profile=/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1
```

## Quality Gate

A Profile passes the runtime quality gate only when:

- all eight core semantics resolve to real bones;
- its Skeleton is loadable;
- its signature matches the current reference skeleton and Profile version;
- all declared axis bases are valid;
- all IK and finger-pose references use declared semantics.

Finger mapping, complete axis coverage, and IK chains are reported separately
so a reduced upper-body Profile can be evaluated honestly. Passing automated
checks does not replace PIE visual approval.

The shipped Manny Profile materializes the complete reviewed `open` and
`point` finger calibration in the asset. Its generated quality report has full
core, finger-bone, finger-pose, axis, IK, and signature coverage.

## Manny And Quinn

`SKM_Manny` and `SKM_Quinn` reference the same UE5 Mannequin `USkeleton` in the
demo project. Both therefore use `ue5_manny.v1`, including the same Skeleton
signature and procedural templates. Phase 7 automation verifies this shared
compatibility instead of creating a misleading duplicate Quinn Profile.

## Verification

- UE 5.3.2 `LLMNPCDemoEditor Win64 Development` reflection/build pass.
- `LLMNPCActionLayer.Phase7`: 3 of 3 tests passed.
- Full `LLMNPCActionLayer` regression suite: 45 of 45 tests passed.
