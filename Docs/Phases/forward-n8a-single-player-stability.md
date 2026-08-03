# Forward N8-A: Single-Player Stability Gate

Status: accepted. Implementation, automation, PIE Smoke, focused regression,
and the corrected formal machine and human gates are complete.

N8-A turns the single-player stability requirements into a fixed editor gate.
It drives Published Manny templates directly through the runtime component and
waits for each action to play and recover before submitting the next request.

## Profiles

Two profiles use the same scheduler and verdict logic:

- `smoke`: 8 completed requests, at least 20 seconds, and at least 4 distinct
  non-target Published templates. This validates the test setup only.
- `formal_30m_500`: exactly 500 completed requests, at least 1800 seconds, and
  at least 6 distinct non-target Published templates. This is the N8 release
  gate and cannot be reduced below either formal budget.

Requests are distributed across the required duration. Every request varies
bounded amplitude, speed, duration, style, seed, and mirror state where the
template policy permits it. A request is complete only after runtime activity
was observed, the procedural and Animation Asset players became idle, and the
recovery dwell elapsed.

## Failure Conditions

The machine gate fails closed when any of the following occurs:

- a valid Published-template submission is rejected;
- playback is never observed or exceeds its timeout;
- the selected PIE actor or an initially installed post-process instance is
  lost;
- action-controlled shoulder, arm, wrist, IK, palm, or finger output remains
  after recovery;
- the duration, request count, or distinct-template budget is incomplete;
- frame sampling or the report state is invalid.

Ambient breathing, head drift, and gaze are excluded from action-pose residue
because they are intentional micro motion. Frame time, 50/100 ms hitches, queue
depth, active-plan count, process physical memory, and per-request results are
still recorded for diagnosis without inventing an unmeasured performance limit.

## Reports And Recovery

The console writes a sanitized checkpoint at startup, every five seconds, and
after request transitions. A crash or PIE loss therefore leaves a partial
`running` or failed report rather than erasing the session. Reports are stored
under:

```text
Saved/LLMNPCActionLayer/ForwardN8/Reports
```

Schema: `llmnpc.forward_n8_stability.v1`.

Machine completion does not grant visual approval. `Human Visual Pass` and
`Human Visual Fail` create a separately named report with `editor_user`
provenance. The Smoke result cannot substitute for the formal profile.

## Automated Coverage

`LLMNPCActionLayer.ForwardN8A` locks:

- Smoke and formal profile budgets;
- fail-closed duration, request, rejection, playback, recovery, and coverage
  verdicts;
- micro-motion-safe action residue measurement;
- frame and memory statistic calculation;
- report sanitization and human-review provenance.

Current result: 4 of 4 targeted tests and 135 of 135 full plugin tests passed.

The first real PIE Smoke run completed on 2026-08-02 with 8 of 8 submissions
accepted, observed, completed, and pose-recovered across 8 distinct templates.
It ran for 22.50 seconds with no rejection, timeout, residual-pose failure,
report error, or warning. The report recorded 17.22 ms P95 frame time and an
11.8 MB process physical-memory delta. These Smoke metrics validate the runner
but do not satisfy the formal release gate.

## First Formal Result And Mirror Remediation

The first `formal_30m_500` run completed on 2026-08-03. Its machine gate
passed after 1800.01 seconds with 500 of 500 requests accepted, observed,
completed, and pose-recovered. It covered at least six templates, reported no
rejection or pose-recovery failure, and recorded 17.08 ms P95 frame time. The
human review was explicitly marked failed because
`gesture.wave.right.manny.procedural.v1` sometimes moved the left arm and was
visually deformed.

The report showed that this comparison-only right-hand template ran 55 times:
28 mirrored and 27 unmirrored. The template incorrectly advertised mirror
support, so the stability scheduler legitimately exercised a left-hand form
that had never been visually approved. The correction is deliberately narrow:

- the procedural comparison variant now sets `allow_mirror=false`;
- its semantic version is `1.1.1` and catalog revision is `3`;
- its source JSON and UAsset share the rebuilt content hash;
- the stability trace now displays the current template, style, and mirror
  decision;
- `LegacyWaveMirrorPolicy` prevents this right-hand-only variant from
  regaining mirror support while preserving its exclusion from normal model
  selection.

The failed formal report remains immutable evidence at
`stability_20260803_012339_formal_30m_500_human_failed.json`.

## Accepted Formal Result

The corrected right-hand procedural wave passed its focused PIE review. A
fresh `formal_30m_500` run then completed on 2026-08-03 after 1800.02 seconds.
All 500 requests were accepted, observed in playback, completed, and recovered
across nine Published templates. The run recorded:

- zero rejected requests, recovery failures, actor losses, post-process losses,
  report errors, and warnings;
- zero residual action pose, a maximum queue depth of zero, and at most one
  active plan;
- 17.71 ms P95 frame time, one 50 ms hitch, and one 100 ms hitch from the same
  125 ms maximum frame;
- 2950.11 MB starting, 2952.38 MB peak, and 1414.16 MB ending process physical
  memory, with no inferred leak verdict attached to operating-system working
  set movement.

The strict right-hand procedural comparison variant ran 55 times and remained
`mirror=false` in all 55 requests. Other templates exercised valid mirroring
82 times, proving that the correction did not disable mirroring globally.

The user explicitly accepted the complete run through `Human Visual Pass`. The
accepted evidence is
`stability_20260803_024302_formal_30m_500_human_passed.json`; its human-review
source is `editor_user`. N8-A is therefore accepted for the next stable
checkpoint.
