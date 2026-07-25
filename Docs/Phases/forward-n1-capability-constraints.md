# Forward N1: Capability Snapshot And Kinematic Constraints

## Status

- Release: `v0.10.0-alpha.2`
- Profile: `ue5_manny.v1`
- Profile semantic version: `1.1.1`
- Finger-pose calibration revision: `2`
- Human visual gate: passed on 2026-07-25
- Multiplayer scope: deferred

## Delivered

- Versioned `llmnpc.skeleton_capability.v1` schema and deterministic exporter.
- Model-safe Manny Capability View without bone names, transforms, quaternion
  data, Compact Pose indices, or private control IDs.
- Shoulder/clavicle bindings and animation-thread snapshot output.
- Calibrated Open, Point, Relaxed, and Curl poses for all Manny fingers.
- Per-control angular, positional, and normalized speed, acceleration, and jerk
  constraints.
- IK reach, continuity, finite-value, collision-proxy, and lower-body ownership
  checks.
- Sorted, unique final `FBoneTransform` output contract.
- Sanitized real-provider Capability Smoke with strict exact-set validation.
- Human-gated Manny Baseline calibration and approval command.

## Approved Artifacts

```text
Capability Hash:
md5:9a0b995dac43d28ce4a674fa79d6d532

Manny Validation Baseline Hash:
md5:2e346854d3be9b09e8a666484b5821da

Skeleton Signature:
md5:6b1ff1901c28611b8c817ec9c968a58f
```

The approved Baseline covers five Published Manny procedural templates. All
five pass their calibrated diagnostic limits with no remaining issues. Limits
were derived from the accepted template distributions with a 1.2 headroom
multiplier and then written into the Skeleton Profile.

Approval metadata is excluded from the behavior Capability Hash. Constraint,
finger-pose, axis, reach, collision, manifest, or semantic-capability changes
still invalidate it.

## Human PIE Review

The fixed Manny test map and Motion Test Console were used to verify:

- bilateral shoulder output is visible, stable, and returns to the base pose;
- Relaxed Hand is naturally open and visually distinct from Curl Hand;
- Curl Hand produces a clear closed hand without finger detachment;
- interruption and recovery return cleanly to the base animation;
- existing Nod, Point, FK Wave, procedural Wave, and subtle Wave behavior
  remains acceptable.

The shoulder button is an isolated control-channel review. A complete
expressive Shrug recipe, including chest, elbows, and palms, belongs to Phase
N5.

## Automated Verification

```text
Forward N1 tests: 8/8
Full LLMNPCActionLayer suite: 70/70
```

The Forward N1 suite covers:

- Capability determinism and restricted-field scanning;
- behavior hash invalidation and approval-metadata hash stability;
- shoulder/finger sampling and visual-review fixtures;
- Profile coverage and calibration revision;
- output ordering and animation-thread-safe snapshot properties;
- kinematic limits and derivative sampling;
- approved Baseline/Profile hash agreement;
- offline Capability Smoke response contracts.

## Online Verification

Provider: `deepseek_direct_editor`

Model: `deepseek-v4-flash`

The final online smoke passed against the approved Capability Hash:

```text
Status: passed
HTTP status: 200
Attempts: 1
Latency: 3.843 seconds
Expected/observed capabilities: hand.pose.curl, hand.pose.open,
hand.pose.point, hand.pose.relaxed
Restricted-field scan: passed
Private-identifier scan: passed
```

The configured provider, model, and non-secret configuration hashes all
matched. Raw requests, responses, credentials, and headers were not persisted.
The sanitized local evidence report is
`Saved/LLMNPCActionLayer/ForwardN1/Reports/capability_smoke_20260725_031801.json`;
`Saved` remains outside the release.

## Reproduction

After a behavior-affecting Profile or constraint change:

```text
LLMNPC.RefreshMannyN1Profile
```

Run PIE visual review. After explicit human acceptance:

```text
LLMNPC.ApproveMannyN1ValidationBaseline
```

Then run:

```text
Automation RunTests LLMNPCActionLayer
LLMNPC.RunMannyN1CapabilitySmoke
```

Any Capability or Baseline Hash change invalidates this acceptance record and
requires a new human review before release.
