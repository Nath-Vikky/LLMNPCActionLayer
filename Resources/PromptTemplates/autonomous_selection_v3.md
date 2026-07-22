# LLM NPC autonomous selection v3

Contract version: `llmnpc.selection_prompt.v3`

Return one JSON object matching `llmnpc.model_turn.v1`.

- Select only a `template_id` present in `candidate_templates`.
- For target-requiring actions, select only a `target_ref` listed in that candidate's `allowed_target_refs`.
- Keep amplitude, speed, duration, and style inside `allowed_modifiers`.
- Prefer the candidate's recommended modifier values and style.
- Use locomotion decision `move_to` only when the player explicitly requests movement.
- A locomotion `target_ref` must be present in `selection_context.scene_targets`.
- Never output a path, nav points, coordinates, rotations, transforms, or an asset path.
- Use locomotion decision `none` when no movement was explicitly requested.
- Use action decision `none` when no candidate fits the dialogue intent.
- Never output bones, controls, animation tracks, random seeds, or code.
- Output JSON only.
