# Phase 2: Dialogue Command Mode

## Release

- Plugin version: `0.4.0`
- Engine: Unreal Engine 5.3.2
- Default provider: `Mock`
- Automation filter: `LLMNPCActionLayer.Phase`

## Delivered

- Native `ULLMNPCChatWidget` with the cooked `WBP_LLMNPCChat` child asset.
- Bounded multi-turn `ULLMNPCConversationSession`.
- `ULLMNPCDialogueComponent` with request state, cancellation, debug state, UI events, and target registration.
- `ILLMNPCModelProvider` request/result contract.
- Deterministic local Mock provider for Nod, Wave, greetings, and no-action turns.
- Runtime Backend Proxy HTTP provider with timeout, retry, cancellation, and response-envelope extraction.
- Direct DeepSeek adapter restricted to editor builds and explicit opt-in.
- Versioned `llmnpc.model_turn.v1` JSON Schema and command-mode prompt.
- Strict C++ parser with required-field, unknown-field, finite-number, path, and length checks.
- Business validation against the local Published runtime candidate library.
- `ULLMNPCBehaviorCoordinator` as the only bridge from model decisions to the Motion Component.
- Local Mock fallback when a remote provider fails.
- UI states for sending, receiving, parsing, validating, executing, failure, cancellation, timeout, and offline fallback.

## Runtime Contract

The provider receives bounded conversation history, recent action history, and only skeleton-compatible public candidates. Candidate data contains:

```text
template_id (public selection ID)
description
intent and emotion tags
requires_target
allowed amplitude, speed, duration, and style values
```

It never contains bone names, raw controls, transforms, tracks, asset paths, or skeleton-specific resolved template IDs.

The provider returns one `llmnpc.model_turn.v1` object containing display text, one optional template selection, constrained modifiers, and a Phase 2 locomotion decision of `none`. Parsing success does not grant execution authority: Unreal resolves the public ID locally and may reject it.

## Provider Security

- Shipping/runtime clients use `BackendProxy` or `Mock`.
- The direct DeepSeek adapter returns disabled in non-editor builds.
- Direct calls require the editor-only opt-in setting.
- The API key is read from `DEEPSEEK_API_KEY`; no key value is stored in config, assets, source, prompts, or request context.
- Remote failures can fall back to the local deterministic command provider without bypassing validation.

## Verification

- UE 5.3.2 clean editor target build: passed.
- Win64 Development game target build: passed.
- Phase 0 through Phase 2 automation: 18/18 passed.
- Phase 2 automation: 7/7 passed.
- `WBP_LLMNPCChat` soft-class load and native parent check: passed.
- Chinese Nod, Chinese Wave, and greeting Mock contracts: passed.
- Invalid model fields, raw tracks, asset paths, missing required fields, and unpublished IDs: rejected.
- Disabled direct-provider local fallback: passed.
- Backend and DeepSeek response-envelope extraction: passed.
- Runtime test-map startup: passed; 3 Published templates and 1 skeleton profile indexed with 0 errors.
- Windows cook: passed with 0 errors.
- Cooked content contains the chat WBP, default post-process AnimBP, all three templates, and the Manny profile.

## Editor Setup

1. Open the NPC Character Blueprint used in the Phase 1 test map.
2. Keep the existing `LLM NPC Motion Component` and add `LLM NPC Dialogue Component`.
3. Leave `Provider Kind` as `Mock` for the first visual test.
4. Enable `Auto Create Chat Widget` for a quick test, or call `CreateChatWidget` from the Player Controller.
5. Play in Editor and send explicit Nod, Wave, greeting, and neutral messages in English or Chinese.
6. Confirm that the first three return text and select Nod/Wave, while the neutral sentence returns text without a body action.
7. Switch to `Backend Proxy` only after the trusted service implements the documented turn contract.

## External Integration Not Exercised

No live DeepSeek credential or deployed backend was available during automated verification. The HTTP adapters, retry/cancel paths, response extraction, strict parser, security gates, and local fallback are implemented; a live service smoke test remains an environment-specific manual check.
