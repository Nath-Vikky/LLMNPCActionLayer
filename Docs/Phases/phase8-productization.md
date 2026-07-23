# Phase 8: Productization

Version: `0.9.0`

Phase 8 establishes a distributable runtime boundary. It does not give the
model more authority: the network, provider, LOD, and recovery layers all carry
the same constrained template IDs and validated motion plans.

## Runtime Packaging

The plugin is verified as an isolated HostProject plugin, not only as part of
the demo Editor target:

```powershell
RunUAT.bat BuildPlugin `
  -Plugin=<Project>/Plugins/LLMNPCActionLayer/LLMNPCActionLayer.uplugin `
  -Package=<Output>/LLMNPCActionLayer `
  -TargetPlatforms=Win64 `
  -Rocket
```

The gate builds:

- `UnrealEditor Win64 Development`;
- `UnrealGame Win64 Development`;
- `UnrealGame Win64 Shipping`;
- the final filtered plugin archive.

`Config/FilterPlugin.ini` includes runtime configuration, this documentation,
and the README in the archive. It excludes project logs, Saved state, Git data,
and the temporary HostProject.

## Provider Extension

`FLLMNPCModelProviderRegistry` owns provider factories by stable `ProviderId`.
The Runtime module registers `mock`, `backend_proxy`, and
`deepseek_direct_editor`. Another runtime module can register its own provider
without changing `ULLMNPCDialogueComponent`.

Use `ProviderIdOverride` or `SetProviderId` for one NPC, or
`DefaultProviderId` in project settings. Unknown IDs fail closed instead of
silently becoming Mock. The legacy enum remains supported.

Provider callbacks may be asynchronous, but must complete once or honor
`CancelRequest`. `DialogueRequestWatchdogSeconds` recovers a request that never
completes. The local Mock fallback is attempted at most once.

## Protocol Compatibility

`FLLMNPCProtocolCompatibility` is the single version boundary:

- request: `llmnpc.turn_request.v2`;
- response: `llmnpc.model_turn.v1`;
- current selection prompt: `llmnpc.selection_prompt.v3`;
- motion plan: `1.0`.

Selection prompts v1-v3 remain recognized. Motion plan `1` and `1.0.0` migrate
to `1.0`. Unknown response schemas and unknown motion-plan versions are
rejected with stable error codes.

## Network Replication

`ULLMNPCMotionComponent` is replication-ready by default when its owning Actor
replicates. The server replicates one bounded high-level command when execution
starts:

- a validated `FLLMMotionPlan`; or
- a Published animation template ID plus constrained modifiers;
- opaque Target refs mapped to replicated Actors;
- a synchronized server start time and command sequence.

Evaluated pose snapshots, bone transforms, Skeleton assets, and model prompts
are never replicated. Clients validate the command again and compensate for
network age. Late non-looping commands that have already ended are dropped.

With `bRequireAuthorityForReplicatedSubmission`, a replicated NPC accepts new
commands only on authority. Dialogue/model selection should therefore run on
the server. Target Actors must also replicate and be network relevant.

Dedicated servers do not install the post-process AnimBP or evaluate
procedural bone poses. They validate, schedule, and replicate the command.

## Motion LOD And Profiling

Automatic Motion LOD uses distance to the nearest local view target:

| Level | Default distance | Behavior |
| --- | ---: | --- |
| Full | 0-2000 cm | Every component tick, including micro motion |
| Reduced | 2000-6000 cm | Accumulated sampling at about 15 Hz |
| Minimal | Beyond 6000 cm | Accumulated sampling at 4 Hz, no micro motion |

The accumulated delta preserves clip time; LOD changes do not slow the action.
Dedicated servers use Minimal scheduling without pose evaluation.

Unreal Insights/stat commands expose:

- `STATGROUP_LLMNPCActionLayer`;
- `Motion Component Tick`;
- `Motion Sampling`.

`GetDebugState` also reports the current LOD, update interval, and replicated
command sequence.

## Privacy And Recovery

Local selection telemetry is opt-in through
`bEnableLocalSelectionTelemetry`. It remains an in-process bounded ring buffer
and is never uploaded by the plugin. API secrets remain editor-session or
environment data and are not serialized into assets or replicated.

Request cancellation clears the watchdog, provider request, fallback provider,
candidate state, and analytics state. Stale provider callbacks are ignored by
request ID.

## Verification

- UE 5.3.2 Editor Development build.
- `LLMNPCActionLayer.Phase8`: 5 of 5 tests.
- Full `LLMNPCActionLayer` regression suite: 50 of 50 tests.
- Isolated Win64 `BuildPlugin`, including Development and Shipping Game targets.
- Human-owned two-player PIE visual approval remains required for appearance,
  montage slots, target relevance, and network latency feel.
