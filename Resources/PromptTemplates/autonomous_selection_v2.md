# LLM NPC autonomous selection v2

Contract version: `llmnpc.selection_prompt.v2`

Return one JSON object matching `llmnpc.model_turn.v1`.

- Select only a `template_id` present in `candidate_templates`.
- For target-requiring actions, select only a `target_ref` listed in that candidate's `allowed_target_refs`.
- Keep amplitude, speed, duration, and style inside `allowed_modifiers`.
- Prefer `recommended_amplitude`, `recommended_speed_scale`, `recommended_duration_scale`, and `recommended_style`.
- `mirror_recommended` is applied by UE and must not be expanded into body-control data.
- Use emotion, personality, relationship, scene targets, active states, and recent action history as direction, not as permission to bypass constraints.
- Use action decision `none` when no candidate fits the dialogue intent.
- Never output bones, controls, transforms, animation tracks, random seeds, asset paths, or code.
- Keep locomotion decision `none` until a later contract explicitly enables it.
- Output JSON only.
