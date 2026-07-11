# Phase 6 Provider Configuration Slice

Version: `0.7.2`

This slice adds the first user-facing configuration surface for model providers. It does not complete the animation-asset and navigation work planned for Phase 6.

## Editor workflow

Open `Tools > LLM NPC Provider Settings` to:

- choose Mock, Backend Proxy, or DeepSeek Direct (Editor) as the project default;
- configure endpoints, model, environment-variable name, timeout, retry count, temperature, and token limit;
- enter a masked DeepSeek API key for the current editor process only;
- test the DeepSeek connection with a structured no-action request;
- apply the selected provider to selected actors that own an `LLMNPCDialogueComponent`.

## Credential boundary

- API key text is never written to project config, assets, or logs.
- The masked field writes only to an in-memory editor-session credential store.
- The environment variable remains the recommended persistent credential source.
- Direct DeepSeek calls still require explicit editor-only opt-in and remain unavailable to packaged runtime builds.

## Verification

- Automation filter: `LLMNPCActionLayer.Phase6.Provider`
- Editor and game targets must compile.
- Full plugin automation, test-map smoke launch, and Windows cook remain release gates.

## Remaining Phase 6 work

- approved animation asset aliases and Montage/Sequence playback;
- compound behavior orchestration;
- navigation intent, AI MoveTo, and target-facing control;
- behavior timeout and interaction-channel arbitration;
- procedural gesture overlays on asset-driven motion.
