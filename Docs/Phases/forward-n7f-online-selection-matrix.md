# Forward N7-F: Strict Online Selection Matrix

Status: complete on 2026-08-02.

N7-F closes the Manny action-library expansion phase with a fixed, repeatable
online selection gate. The gate uses the configured real provider in PIE and
does not permit local fallback to satisfy a case.

## Locked Matrix

The matrix contains 17 cases and covers all eight Published Public Actions:

- `gesture.nod`
- `gesture.wave.right`
- `gesture.point.target`
- `gesture.clap`
- `gesture.shrug`
- `gesture.beckon`
- `gesture.present`
- `gesture.thumbs_up`

The cases also cover `None`, target-present and target-missing behavior,
left/right hand occupation, mirroring, neutral/friendly/subtle/excited styles,
walking, and repeat suppression. The accompanying Manny library audit rejects
missing actions, incomplete candidates, non-Manny substitutions, and templates
that are not Published.

## Playback Gate

A valid model response is not treated as a completed visual case. For every
case that starts motion, the console waits until runtime activity is observed,
the active plan and queued work finish, the animation player becomes idle, and
a 0.9 second recovery dwell completes. A 20 second timeout fails the case.
Cases that correctly select no action use a short observation window instead.

Repeat suppression defaults to 6 seconds so the seed action remains excluded
after its complete playback and recovery interval. The matrix preflight rejects
project settings below 5.5 seconds.

## Final Evidence

The accepted report is written outside the plugin source tree at:

```text
Saved/LLMNPCActionLayer/ForwardN7/Reports/selection_matrix_20260802_052641_human_passed.json
```

Final results:

- Machine verdict: 17 of 17 cases passed.
- Required playback: 14 of 14 observed and completed.
- Playback timeouts: 0.
- Schema failures: 0.
- Local fallback usage: 0.
- Missing coverage tags: 0.
- Manny library audit: passed for all eight Public Actions.
- Human visual review: 17 of 17 passed by `editor_user`.
- Targeted `LLMNPCActionLayer.ForwardN7F` automation: 3 of 3 passed.
- Full plugin automation: 131 of 131 passed.

The report stores provider identity, configuration hash, request/result
metadata, and human-review provenance. It does not store the API credential.
