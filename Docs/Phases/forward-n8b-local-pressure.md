# Forward N8-B: Local Multi-NPC Pressure Gate

Status: accepted. Implementation, compilation, automation, all three machine
PIE profiles, and explicit human visual review are complete.

N8-B extends the single-NPC N8-A scheduler into a concurrent local pressure
gate. It uses the selected PIE Manny as the source instance and class, then
creates only transient runtime actors. It never edits the test map or saves the
spawned actors into a level asset.

## Locked Profiles

All profiles submit one valid non-target Published template to every managed
NPC in a round, observe playback on every NPC, and wait for every action to
finish and recover before the next round.

- `smoke_3_npc`: 3 managed NPCs, 2 rounds, 6 requests, and at least 10 seconds.
  At least 2 actors must exercise Full motion LOD. This validates setup only.
- `local_10_npc`: 10 managed NPCs, 5 rounds, 50 requests, and at least 60
  seconds. At least 8 actors must exercise Full motion LOD.
- `lod_30_npc`: 30 managed NPCs, 4 rounds, 120 requests, and at least 90
  seconds. At least 8 actors each must exercise Full, Reduced, and Minimal
  motion LOD.

The formal 10/30-NPC budgets cannot be reduced through UI parameters. The
runner rotates Published templates and bounded amplitude, speed, duration,
style, seed, and policy-permitted mirror values across actors and rounds.

## Runtime Actor Ownership

The currently selected Manny is managed but never destroyed. The runner spawns
the remaining 2, 9, or 29 actors from that runtime class using Always Spawn,
marks them transient and with `LLMNPC.N8PressureSpawned`, disables collision and
character movement, and keeps their skeletal meshes evaluating even when they
are not visible.

The 3/10-NPC actors are placed inside Full-quality distance. The 30-NPC actors
are distributed relative to the local View Target across Full, Reduced, and
Minimal distance bands. Actual LOD is read back from every Motion Component;
placement intent alone does not satisfy coverage.

Each transient actor first prefers a facing direction toward the View Target.
Before it joins the run, the runner performs the same 95 cm left/right shoulder
sphere sweeps used by runtime obstacle adaptation. A blocked candidate rotates
through bounded alternate facings and then tries other points at the same LOD
radius. The safety resolver remains enabled; unavailable clearance fails actor
setup instead of being hidden by disabling obstacle checks.

Normal completion, cancellation, actor selection changes, console refresh,
console destruction, and PIE loss all use the same cleanup path. Only actors
spawned by this runner are destroyed. Cleanup count is part of the machine
gate.

## Machine Gate

The report fails closed for:

- managed or spawned actor count mismatch;
- a missing actor or Motion Component, or loss of a component-owned Post
  Process instance that was installed when the actor joined the run;
- any rejected valid Published-template request;
- playback not observed, timeout, incomplete playback, or residual action
  pose after the recovery dwell;
- incomplete round, request, template, or LOD coverage;
- per-NPC queue growth beyond the one request currently awaiting an LOD update;
- a queue or active plan that remains at a round boundary or final cleanup;
- incomplete cleanup of runner-owned actors;
- missing frame samples or an invalid report state.

Submitting to a Minimal LOD NPC can legitimately leave one request pending
until its next 0.25 second update. The gate records those transient dispatch
waits, but requires every per-NPC queue to stay at depth one or lower and every
round/final boundary to drain to zero. This distinguishes LOD scheduling latency
from real backlog growth.

Frame time, 50/100 ms hitches, aggregate active plans, transient and boundary
queue depth, process
physical memory, per-actor LOD and recovery, and every request are recorded.
Each actor also records whether a component-owned Post Process instance was
present at the baseline, whether it remained installed, and any installation
diagnostic. Manny's current main-AnimGraph procedural node plus native skeletal
mesh Pose Drivers is a valid path and is not misreported as a missing optional
component override.
The newest accepted N8-A formal report is loaded as the single-NPC P95 frame
baseline when available. N8-B records the absolute delta and ratio, but does
not invent a machine-specific performance failure threshold before enough
baseline evidence exists. Missing baseline data and more than 512 MB physical
memory growth are warnings, not fabricated hard limits.

## Reports And Human Review

Reports are sanitized checkpoints using schema
`llmnpc.forward_n8_pressure.v1` and are stored under:

```text
Saved/LLMNPCActionLayer/ForwardN8/Reports
```

The console writes at startup, after actor creation, every five seconds, after
round transitions, and at completion. `Visual Pass` and `Visual Fail` create a
separate report with `editor_user` provenance. Machine completion never grants
visual approval automatically.

## Automated Coverage

`LLMNPCActionLayer.ForwardN8B` locks:

- Smoke, 10-NPC, and 30-NPC formal budgets;
- actor, request, round, cleanup, and queue verdicts;
- Full/Reduced/Minimal LOD coverage;
- P95 baseline comparison and sanitized human-review reports.

Current result: 4 of 4 targeted tests and 139 of 139 full plugin tests passed.

## PIE Evidence

All results below were produced by the same final N8-B binary and use the
accepted N8-A 30-minute report as their P95 baseline:

| Profile | Result | LOD Full/Reduced/Minimal | P95 vs baseline | Queue evidence |
| --- | --- | --- | --- | --- |
| `smoke_3_npc` | 6/6 in 10.39 s | 3/0/0 | 17.84 ms, 1.008x | peak 0, boundary 0, final 0 |
| `local_10_npc` | 50/50 in 60.03 s | 10/0/0 | 19.13 ms, 1.081x | peak 0, boundary 0, final 0 |
| `lod_30_npc` | 120/120 in 90.01 s | 11/10/9 | 23.29 ms, 1.315x | transient peak 6 across nine Minimal actors, per actor 1, boundary 0, final 0 |

Every profile completed with zero rejected requests, pose recovery failures,
actor losses, Post Process failures, report errors, and warnings. The 30-NPC
run recorded eight frames with a transient Minimal-LOD dispatch queue. No NPC
exceeded depth one and all four round barriers drained to zero, confirming that
the observation was throttled dispatch latency rather than accumulating work.

The user explicitly accepted the multi-NPC visual result. A final 3-NPC review
run completed 6/6 requests in 10.41 seconds with all scheduler boundaries at
zero and was recorded through the Motion Test Console as `Human Visual Pass`:

```text
pressure_20260803_063041_smoke_3_npc_human_passed.json
```

The reviewed report retains `machine_passed=true`, `human_review=passed`, and
`human_review_source=editor_user`. N8-B therefore satisfies its separate
machine and human gates.
