# LLM NPC Action Layer

LLM-directed, Unreal Engine-executed procedural NPC gesture layer.

The plugin keeps large language models away from low-level skeletal control. The model selects constrained action templates and structured parameters, while Unreal Engine validates, queues, and executes the body expression through animation blueprint, Control Rig, IK, and procedural animation.

## MVP Actions

- `gaze.look_at`
- `gesture.nod`
- `gesture.wave`
- `gesture.point`

## Runtime Flow

```text
ActionPlan JSON
  -> Validator
  -> LLMNPCActionComponent queue
  -> RuntimeGestureState
  -> AnimBP
  -> Control Rig / IK
```

## Main Blueprint API

- `SubmitActionPlanJson`
- `SubmitActionPlan`
- `RequestActionPlanFromContext`
- `RegisterTargetRef`
- `TestNod`
- `TestWave`
- `TestPoint`

## Default HTTP Endpoint

```text
http://localhost:8787/npc/action-plan
```
