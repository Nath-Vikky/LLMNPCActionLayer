# LLMNPCActionLayer 后续完整开发与改进路线（StepImprove）

> 文档版本：1.0  
> 面向版本：Unreal Engine 5.3 / LLMNPCActionLayer 0.2.x  
> 制定日期：2026-07-10  
> 目标仓库：`Nath-Vikky/LLMNPCActionLayer`  
> 相关工具仓库：`Nath-Vikky/UEProjectIntelligence`

---

## 0. 文档目的

本文档定义 `LLMNPCActionLayer` 从当前 V2 程序化动作原型，继续发展为完整 NPC 身体动作系统的后续路线。

最终系统不是让运行时大模型自由操控骨骼，而是形成两条严格分离的流水线：

1. **离线动作生产流水线**  
   使用 Unreal 动画序列、UE Project Intelligence（UEPI）、Codex、程序化动作代码与人工审核，生产可复用、可追溯、可发布的动作模板。

2. **运行时动作消费流水线**  
   使用 DeepSeek 等大模型理解对话和场景，只从已经审核通过的动作模板中做选择，并输出有限、结构化、可校验的风格参数；UE 负责状态判断、动作调度、程序动画、Animation Sequence、IK、骨骼约束和最终 Pose。

核心原则保持不变：

```text
LLM 做动作导演和模板选择
UE 做动作执行、约束、动画、寻路和物理
Codex / UEPI 做离线动作工程与模板生产
人工审核决定哪些动作可以进入正式模板库
```

---

## 1. 对现有设想的总体判断

现有 7 点设想是可行的，而且彼此可以形成完整产品闭环。建议补充三个关键原则。

### 1.1 运行时模型与离线工程模型必须隔离

运行时 DeepSeek：

- 可以回复文本；
- 可以选择已批准的 `TemplateId`；
- 可以选择目标引用 `TargetRef`；
- 可以在模板允许的范围内调整速度、幅度、情绪强度等参数；
- 后期可以提出走向某个目标的高层意图。

运行时 DeepSeek **不可以**：

- 输出骨骼名；
- 输出任意 FK 骨骼旋转；
- 输出任意 IK 世界坐标；
- 输出逐帧动画数据；
- 创建或修改模板；
- 调用 Codex、MCP 或工程编辑工具；
- 绕过 UE Validator、Scheduler 和角色状态机。

离线 Codex / UEPI：

- 可以读取动画重建数据；
- 可以分析 driver bones；
- 可以编写或调整程序化动作代码；
- 可以生成草稿模板；
- 可以读取全骨骼姿态样本做对比；
- 可以协助修复插件和生成测试工具；
- 但不能自动将模板标记为正式可用。

### 1.2 动作模板库需要“草稿—审核—发布”生命周期

不能把 Codex 生成的 JSON 直接放进运行时模板库。推荐生命周期：

```text
Draft
  ↓
Generated
  ↓
StaticValidated
  ↓
PreviewReady
  ↓
HumanApproved
  ↓
Published
  ↓
Deprecated / Rejected
```

只有 `Published` 模板进入 Shipping 构建和运行时检索目录。

### 1.3 复杂动画资产与程序动画应长期共存

“复杂动作以后全部改造成程序动画”可以作为研究方向，但不应该成为强制目标。

长期建议同时支持：

```text
Procedural
AnimationAsset
Hybrid
LocomotionIntent
```

其中：

- 点头、挥手、招手、指向、耸肩适合程序化；
- 行走、跑步、跳跃、攻击、坐下、舞蹈等复杂全身动作，第一阶段优先使用 Animation Sequence / Montage / Locomotion 系统；
- Hybrid 模式可以使用动画资产作为主体，再叠加头部看向、手指、胸腔、情绪幅度等程序层；
- 只有当某个复杂动画确实有高动态适配价值时，再投入成本将其转成程序模板。

---

## 2. 三个信任区域

系统应明确划分三个信任区域。

### 2.1 Zone A：离线动作生产区

参与者：

```text
UE Editor
UEProjectIntelligence
MCP
Codex
程序员
动画序列
测试关卡
```

权限：

- 读取工程动画资产；
- 读取 UEPI Snapshot 和 artifact；
- 生成 MotionTemplate 草稿；
- 修改 C++；
- 生成测试代码；
- 创建导入计划；
- 运行自动检查；
- 输出审核候选。

这一区域权限高，但只在开发环境存在。

### 2.2 Zone B：审核与发布区

参与者：

```text
Template Reviewer
Editor Review Tool
Automated Validator
Pose Comparison Tool
Source Control
Template Publisher
```

职责：

- 加载草稿模板；
- 在 Manny 上预览；
- 与原动画对比；
- 检查穿插、关节超限、返回 Idle 连续性；
- 人工批准或拒绝；
- 生成版本号、哈希和发布记录；
- 将批准模板转换为 Runtime Asset。

### 2.3 Zone C：运行时低权限区

参与者：

```text
UMG Chat
Conversation Component
DeepSeek Provider
Action Decision Validator
Template Registry
Action Scheduler
Motion Runtime
AnimNode / AnimSequence / Navigation
```

权限：

- 获取 NPC 语义上下文；
- 查询可用模板的元数据；
- 让模型在候选模板中选择；
- 绑定合法 TargetRef；
- 调整受限风格参数；
- 执行已发布动作。

禁止：

- 读取 UEPI 全骨骼 artifact；
- 修改模板；
- 生成代码；
- 直接改骨骼轨道；
- 访问工程写工具。

---

## 3. 总体目标架构

### 3.1 离线动作生产流水线

```text
UAnimSequence
    ↓
UEPI Snapshot Scan
    ↓
bone_motion_profile
reconstruction_profile
driver_track_curves
full_pose_artifact（只用于高精度验证）
    ↓
Codex 分析和动作复刻
    ↓
Draft MotionTemplate JSON
或插件代码 / Solver 改进
    ↓
Template Importer
    ↓
Static Validator
    ↓
UE Test Map 实机预览
    ↓
自动 Pose / End Effector 指标
    ↓
人工审核
    ↓
Template Publisher
    ↓
Published UPrimaryDataAsset + Runtime Registry
```

### 3.2 运行时对话与动作流水线

```text
玩家输入文字
    ↓
WBP_LLMNPCChat
    ↓
ULLMNPCConversationComponent
    ↓
Context Builder
    ├─ 当前对话历史
    ├─ NPC 情绪
    ├─ NPC 性格
    ├─ 与玩家关系
    ├─ 当前身体状态
    ├─ 场景 TargetRef
    └─ UE 预筛选后的动作候选
    ↓
ILLMNPCModelProvider / DeepSeek
    ↓
Canonical ActionDecision JSON
    ↓
Decision Validator
    ↓
Action Scheduler
    ├─ Procedural Template
    ├─ AnimationAsset Template
    ├─ Hybrid Template
    └─ LocomotionIntent
    ↓
UE Animation / Motion Runtime / AIController
```

### 3.3 模型不直接看到完整动作数据

运行时模型只接收动作元数据，例如：

```json
{
  "template_id": "gesture.wave.friendly.manny.v1",
  "display_name": "Friendly Wave",
  "description": "Raise one hand and wave toward a nearby visible person.",
  "intent_tags": ["greeting", "attract_attention"],
  "requires_target": false,
  "allowed_states": ["idle", "walking_upper_body"],
  "style_ranges": {
    "energy": [0.2, 1.0],
    "speed": [0.5, 1.3],
    "openness": [0.4, 1.0]
  }
}
```

不向运行时模型发送：

- 每个骨骼的 keyframe；
- UEPI full pose artifact；
- direct FK track；
- AnimNode 内部骨骼映射；
- 模板源动画路径；
- 插件源码。

---

## 4. UEProjectIntelligence 的定位与接入边界

### 4.1 已确认的 UEPI 动画能力

UEPI 当前能针对动画序列生成三类主要 artifact。

#### Bone Motion Profile

存放路径：

```text
Saved/UEProjectIntelligence/store/artifacts/animation_bone_motion/
```

包含：

- `initial_pose`
- `end_pose`
- `driver_bones`
- `inherited_motion_bones`
- `motion_intent_groups`
- `changed_bones`
- 稀疏姿态样本
- LLM-oriented motion summary

适合：

- 判断真正被本地轨道驱动的骨骼；
- 判断运动意图区域；
- 区分 driver bone 和由父级继承运动的末端骨骼；
- 决定程序化动作应优先控制哪些骨骼或 IK 目标。

#### Reconstruction Profile

存放路径：

```text
Saved/UEProjectIntelligence/store/artifacts/animation_reconstruction/
```

包含：

- `driver_track_curves`
- 每个 driver bone 的 local FK keyframe；
- `normalized_time`
- `phase_estimates`
- `curve_semantics`
- `reconstruction_guidelines`
- 对应 full-pose artifact manifest。

适合：

- Codex 将现有动画序列复刻成程序化 MotionTemplate；
- 判断某条轨道适合 keyframe、spline 还是 oscillator；
- 保留源动作的主要局部 FK 运动。

#### Full Pose Samples

存放路径：

```text
Saved/UEProjectIntelligence/store/artifacts/animation_full_pose_samples/
```

包含每个姿态样本的：

- 所有骨骼；
- parent-relative local transform；
- component-space transform；
- frame number；
- time seconds；
- normalized time。

注意：这不是无限制地把任意长动画逐帧全部发送给模型。当前 UEPI 实现对 full-pose 样本设置了数量上限；较长动画可能等距下采样。它更适合作为验证数据，而不是默认生成输入。

### 4.2 推荐的 MCP 调用流程

Codex 每次处理动画时：

```text
uepi_status
uepi_overview
```

定位动画：

```text
uepi_search {"query": "Waving", "limit": 10}
```

先获取重建主数据：

```text
uepi_animation {
  "asset": "Waving",
  "include": ["summary", "bone_motion_profile", "driver_track_curves"]
}
```

只有需要精确对比或排查误差时才获取：

```text
uepi_animation {
  "asset": "Waving",
  "include": ["full_pose_artifact"]
}
```

### 4.3 与 LLMNPCActionLayer 保持松耦合

`LLMNPCActionLayer` Runtime 模块不应依赖 `UEProjectIntelligence`。

推荐依赖关系：

```text
UEProjectIntelligence
    ↓ 生成标准 JSON artifact
Codex / 外部转换器
    ↓ 生成 LLMNPC Draft Template
LLMNPCActionLayer Editor Importer
    ↓
Published Runtime Template
```

可在后期增加可选 Editor 集成：

```text
Import UEPI Reconstruction Artifact
```

但必须满足：

- 没安装 UEPI 时，LLMNPCActionLayer 仍可正常编译和运行；
- Runtime 构建中不包含 UEPI；
- UEPI 原始 artifact 默认留在 `Saved`，不进入 Shipping；
- 正式库只保存压缩、审核后的动作模板。

---

## 5. 模板库不是简单 RAG 文件夹

第一版不需要立即引入向量数据库。动作模板库应先是一个结构化、可筛选的内容注册表。

### 5.1 第一阶段检索方式

UE 根据确定性条件先筛选：

```text
SkeletonProfile 匹配
ExecutionMode 可用
NPC 状态允许
手部没有被占用
TargetRef 是否存在
是否允许行走时播放
是否在冷却
是否和当前动作通道冲突
剧情是否禁止
```

再根据语义标签筛选：

```text
intent.greeting
intent.agreement
intent.disagreement
intent.pointing
emotion.friendly
emotion.urgent
social.formal
body.upper
```

最后只向模型发送 Top-K 候选。

### 5.2 后续 RAG

当模板很多时，可以在后端或 Editor 侧对以下文本建立 embedding：

- 模板描述；
- 意图标签；
- 适用对白示例；
- 不适用示例；
- 角色风格；
- 情绪风格。

不应向 embedding 服务发送：

- 原始骨骼轨道；
- full pose；
- 未经许可的第三方动画内容；
- 内部工程路径。

运行时仍然只获取 Top-K 模板元数据，而不是整库。

---

## 6. 动作模板分类

建议统一定义：

```cpp
UENUM(BlueprintType)
enum class ELLMNPCTemplateExecutionMode : uint8
{
    Procedural,
    AnimationAsset,
    Hybrid,
    LocomotionIntent
};
```

### 6.1 Procedural

完全由现有 MotionClip / MotionSampler / AnimNode 运行。

适用：

- 点头；
- 摇头；
- 挥手；
- 招手；
- 指向；
- 耸肩；
- 看向；
- 上半身姿态；
- 手指姿态。

### 6.2 AnimationAsset

播放用户配置的：

- Animation Sequence；
- Animation Montage；
- BlendSpace 状态；
- 动画状态机事件。

适用：

- 行走；
- 跑步；
- 跳跃；
- 坐下；
- 起身；
- 攻击；
- 舞蹈；
- 复杂全身交互。

### 6.3 Hybrid

动画资产为主体，程序层增加：

- gaze；
- head look；
- chest style；
- fingers；
- 手部轻微 IK；
- 情绪强度；
- 与目标的方向适配。

### 6.4 LocomotionIntent

模型只输出高层移动意图：

```json
{
  "target_ref": "player",
  "gait": "walk",
  "stop_distance": 160.0,
  "facing_policy": "face_target_after_arrival"
}
```

UE 侧负责：

- AIController；
- NavMesh；
- Path Following；
- CharacterMovement；
- locomotion state；
- 动作中断；
- 碰撞；
- 网络权威。

模型不能输出逐帧位置或路径点。

---

## 7. 模板的作者源文件与运行时资产

推荐采用双层格式。

### 7.1 Authoring JSON

用于：

- Codex 生成；
- Git diff；
- 人工审查；
- Schema 校验；
- 版本控制；
- provenance 保存。

目录建议：

```text
TemplateSources/
  Manny/
    Draft/
    Approved/
    Deprecated/
```

### 7.2 Cooked Runtime Asset

使用 `UPrimaryDataAsset` 或等效 Runtime Asset：

```cpp
UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCMotionTemplate : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName TemplateId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    int32 TemplateVersion = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    ELLMNPCTemplateExecutionMode ExecutionMode =
        ELLMNPCTemplateExecutionMode::Procedural;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName SkeletonProfileId = TEXT("UE5_Manny");

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTagContainer IntentTags;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FGameplayTagContainer EmotionTags;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FLLMMotionClip ProceduralClip;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UAnimationAsset> AnimationAsset;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FLLMNPCStyleParameterDefinition> StyleParameters;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FLLMNPCTemplateRuntimeConstraints RuntimeConstraints;
};
```

Shipping 构建不要依赖在磁盘上动态扫描 loose JSON。Editor Importer 将审核通过的 JSON 转成 Runtime Asset，并生成 Registry。

---

## 8. 建议的动作模板 JSON

```json
{
  "schema_version": "llmnpc.motion_template.v1",
  "template_id": "gesture.nod.neutral.manny.v1",
  "template_version": 1,
  "status": "approved",
  "execution_mode": "procedural",
  "skeleton_profile_id": "UE5_Manny",
  "display_name": "Neutral Nod",
  "description": "A short neutral agreement nod.",
  "semantic": {
    "intent_tags": [
      "intent.agreement",
      "intent.acknowledgement"
    ],
    "emotion_tags": [
      "emotion.neutral",
      "emotion.friendly"
    ],
    "negative_tags": [
      "state.unconscious",
      "state.ragdoll"
    ]
  },
  "constraints": {
    "body_channels": ["head", "gaze"],
    "requires_target": false,
    "can_run_while_moving": true,
    "interruptible": true,
    "priority": 0.35,
    "cooldown_seconds": 0.4
  },
  "style_parameters": {
    "energy": {
      "default": 0.5,
      "min": 0.2,
      "max": 0.9
    },
    "speed": {
      "default": 1.0,
      "min": 0.7,
      "max": 1.3
    }
  },
  "clip": {
    "duration": 0.9,
    "blend_in": 0.12,
    "blend_out": 0.18,
    "tracks": [
      {
        "control_id": "head.pitch",
        "track_type": "oscillator",
        "start_time": 0.05,
        "end_time": 0.82,
        "amplitude": 10.0,
        "frequency": 2.0,
        "phase": 0.0,
        "envelope": "smooth",
        "runtime_parameter_bindings": {
          "amplitude_scale": "energy",
          "time_scale": "speed"
        }
      }
    ]
  },
  "provenance": {
    "creation_method": "existing_runtime_sample_migration",
    "source_animation": "",
    "uepi_artifact_uri": "",
    "uepi_schema_version": "",
    "generator": "LLMNPCActionLayer",
    "generator_version": "0.3.0",
    "source_hash": ""
  },
  "review": {
    "review_state": "approved",
    "reviewer": "",
    "reviewed_at_utc": "",
    "notes": "Migrated from TestNod and verified on UE5 Manny."
  }
}
```

### 8.1 Codex 复刻动画模板示例

Codex 根据 UEPI `driver_track_curves` 生成的模板应额外记录：

```json
{
  "provenance": {
    "creation_method": "uepi_reconstruction",
    "source_animation": "/Game/Animations/Waving.Waving",
    "uepi_artifact_uri": "uepi://animation-reconstruction-profile/...",
    "uepi_schema_version": "uepi.animation_reconstruction_profile.v1",
    "generator": "Codex",
    "generator_version": "recorded",
    "source_hash": "..."
  }
}
```

直接 FK 骨骼轨道可以存在于已审核模板内部，但这些 control 必须设置为：

```text
InternalTemplateOnly
bAllowRuntimeLLM = false
```

---

## 9. 运行时大模型输出协议

运行时 DeepSeek 不输出 `FLLMMotionPlan`，而应输出更高层的 `ActionDecision`。

### 9.1 推荐协议

```json
{
  "schema_version": "llmnpc.action_decision.v1",
  "dialogue_text": "你好，很高兴见到你。",
  "action": {
    "template_id": "gesture.wave.friendly.manny.v1",
    "target_ref": "player",
    "start_delay": 0.1,
    "style": {
      "energy": 0.62,
      "speed": 0.95,
      "openness": 0.8
    }
  },
  "locomotion": null,
  "confidence": 0.92,
  "decision_reason_code": "friendly_greeting",
  "fallback_template_id": "gesture.nod.neutral.manny.v1"
}
```

### 9.2 允许无动作

正常交谈不应该每句话都动：

```json
{
  "action": null
}
```

模型需要学会：

- 当前动作仍在进行时不重复动作；
- 普通事实回答可以不做动作；
- 没有合适模板时返回 null；
- 不为了“看起来智能”而高频抖动。

### 9.3 C++ 数据结构草案

```cpp
USTRUCT(BlueprintType)
struct FLLMNPCStyleValue
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName Name = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Value = 0.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCActionSelection
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName TemplateId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString TargetRef;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StartDelay = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FLLMNPCStyleValue> Style;
};

USTRUCT(BlueprintType)
struct FLLMNPCLocomotionIntent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bEnabled = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString TargetRef;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName Gait = TEXT("walk");

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float StopDistance = 150.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCActionDecision
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString SchemaVersion = TEXT("llmnpc.action_decision.v1");

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DialogueText;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bHasAction = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLLMNPCActionSelection Action;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FLLMNPCLocomotionIntent Locomotion;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Confidence = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName FallbackTemplateId = NAME_None;
};
```

---

## 10. 模型 Provider 抽象

当前 `ULLMNPCAPIClient` 应逐步改成 Provider 架构，避免插件绑定 DeepSeek。

### 10.1 建议类

```text
ULLMNPCModelProvider
    ├─ ULLMNPCDeepSeekProvider
    ├─ ULLMNPCProxyProvider
    └─ ULLMNPCMockProvider
```

### 10.2 抽象类草案

```cpp
DECLARE_DELEGATE_TwoParams(
    FOnLLMNPCDecisionReceived,
    bool,
    const FLLMNPCActionDecision&
);

UCLASS(Abstract)
class LLMNPCACTIONLAYER_API ULLMNPCModelProvider : public UObject
{
    GENERATED_BODY()

public:
    virtual void RequestDecision(
        const FLLMNPCModelRequest& Request,
        FOnLLMNPCDecisionReceived Callback)
        PURE_VIRTUAL(ULLMNPCModelProvider::RequestDecision, );

    virtual void CancelRequest(const FGuid& RequestId)
    {
    }
};
```

### 10.3 为什么需要 Mock Provider

`ULLMNPCMockProvider` 可以按输入命令固定返回：

```text
“挥手” → wave
“点头” → nod
“不要动作” → null
```

这样可以独立测试：

- UMG；
- Conversation；
- Decision Parser；
- Scheduler；
- Template Registry；
- Motion Runtime；

而不依赖网络、额度或模型行为。

---

## 11. DeepSeek 接入方案

### 11.1 配置不要写死模型名

DeepSeek API 兼容 OpenAI 风格的 Chat Completions 接口。模型名称和可用特性会变化，插件必须将以下内容配置化：

```cpp
UPROPERTY(Config, EditAnywhere)
FString BaseUrl = TEXT("https://api.deepseek.com");

UPROPERTY(Config, EditAnywhere)
FString Model;

UPROPERTY(Config, EditAnywhere)
FString ProxyEndpoint;

UPROPERTY(Config, EditAnywhere)
bool bUseBackendProxy = true;

UPROPERTY(Config, EditAnywhere)
bool bAllowDirectProviderInEditor = false;

UPROPERTY(Config, EditAnywhere)
float RequestTimeoutSeconds = 15.0f;

UPROPERTY(Config, EditAnywhere)
int32 MaxConversationTurns = 12;
```

截至本文制定日期，DeepSeek 官方文档列出的模型命名正在变化，因此不能继续假设固定的 `deepseek-chat` 永久存在。

### 11.2 第一版输出模式

推荐先使用：

```json
"response_format": {
  "type": "json_object"
}
```

但这只保证合法 JSON，不保证符合项目 Schema。仍然必须：

- 在 prompt 中明确要求 JSON；
- 提供输出示例；
- 检查空 content；
- 检查 `finish_reason`；
- 处理截断；
- 解析 canonical schema；
- UE 侧再执行 Validator；
- 失败时有限次数重试或进入本地 fallback。

### 11.3 后续 strict tool call

可选增加一个工具：

```text
select_npc_response_and_action
```

参数就是 `ActionDecision` Schema。

DeepSeek strict tool call 当前属于 Beta，应该设计为可选能力：

```text
ProviderCapability.StrictToolCall
```

不能把插件核心运行依赖在 Beta 特性上。

### 11.4 对话历史

DeepSeek Chat Completions 是无状态 API，Conversation Component 必须自行保存并发送历史。

推荐：

```text
短期历史：最近 N 轮
长期状态：压缩后的 conversation summary
确定性状态：当前任务、关系、情绪、剧情变量
```

不要无限增长 messages。

### 11.5 API Key

原型阶段：

```text
Editor / Development 构建
从 DEEPSEEK_API_KEY 环境变量读取
```

正式阶段：

```text
UE Client
    ↓
项目后端代理
    ↓
DeepSeek API
```

Shipping 客户端中不存放真实 API Key。

---

## 12. UMG 文字交流 MVP

### 12.1 第一版目标

只实现：

```text
玩家输入文字
NPC 返回文字
模型从 nod / wave 中选择动作或选择不动作
UE 执行动作
```

第一版不是开放式 AI NPC 产品，只验证：

```text
对话 → 结构化动作决定 → 模板 → 程序动作
```

### 12.2 Widget

建议创建一个插件示例 Widget：

```text
WBP_LLMNPCChat
```

控件：

```text
ConversationScrollBox
UserInputTextBox
SendButton
ConnectionStatusText
CurrentActionText
DebugToggle
```

### 12.3 组件

```text
ULLMNPCConversationComponent
```

职责：

- 保存消息历史；
- 保存 NPC 基础信息；
- 收集当前身体状态；
- 请求模板候选；
- 调用 ModelProvider；
- 更新 UMG；
- 把 ActionDecision 交给 Scheduler。

### 12.4 第一版流程

```text
用户：向我挥手
    ↓
Context Builder：
    command_mode = true
    available_templates = [nod, wave]
    right_hand_free = true
    npc_state = idle
    ↓
DeepSeek：
    dialogue_text = "好的。"
    template_id = wave
    ↓
Decision Validator
    ↓
Motion Template Registry
    ↓
Wave
```

### 12.5 对话和动作应分离处理

即使一次 API 返回两者，代码里仍分成：

```text
Dialogue Output
Action Decision
```

这样未来可以：

- 文本先流式显示；
- 动作稍后开始；
- 文本成功但动作被 UE 拒绝；
- API 动作字段失败时仍保留文本回复。

---

## 13. Context Builder 与模板候选生成

建议新增：

```text
ULLMNPCContextBuilder
ULLMNPCTemplateQueryService
```

### 13.1 上下文来源

```json
{
  "npc": {
    "id": "guard_01",
    "role": "guard",
    "personality": {
      "warmth": 0.3,
      "formality": 0.8,
      "expressiveness": 0.4
    },
    "emotion": {
      "primary": "neutral",
      "intensity": 0.2
    },
    "relationship": {
      "player": "neutral"
    }
  },
  "body_state": {
    "locomotion": "idle",
    "upper_body_free": true,
    "right_hand_free": true,
    "left_hand_free": true,
    "combat": false,
    "stunned": false
  },
  "scene_targets": [
    {
      "ref": "player",
      "type": "character",
      "visible": true,
      "direction": "front",
      "distance": "near"
    }
  ]
}
```

### 13.2 候选动作应由 UE 先过滤

不要把整个模板库交给模型。

```cpp
TArray<const ULLMNPCMotionTemplate*> Candidates =
    TemplateSubsystem->QueryTemplates(Query);

Candidates = Candidates.Left(MaxCandidatesForModel);
```

建议第一版 `MaxCandidatesForModel` 为较小可配置值，例如 8 到 16。

### 13.3 模型选择后再次验证

UE 必须检查：

- 返回 ID 是否在本次候选中；
- Template 是否 Published；
- Skeleton 是否兼容；
- TargetRef 是否注册；
- style 参数是否存在；
- style 值是否在模板范围内；
- NPC 当前状态是否仍允许；
- 冷却是否满足；
- 通道是否空闲。

---

## 14. 动作调度器

现有 `Priority` 和 `bInterruptible` 已进入数据结构，但还没有形成完整抢占逻辑。建议新增：

```text
ULLMNPCActionSchedulerComponent
```

### 14.1 通道

```cpp
UENUM(BlueprintType)
enum class ELLMNPCActionChannel : uint8
{
    Gaze,
    Head,
    Face,
    Chest,
    LeftArm,
    RightArm,
    LeftHand,
    RightHand,
    UpperBody,
    FullBody,
    Locomotion,
    Interaction
};
```

### 14.2 Scheduler 职责

- 判断动作通道冲突；
- 比较优先级；
- 处理中断；
- 处理 cooldown；
- 延迟启动；
- 处理动作完成；
- 处理动作失败；
- 管理 locomotion 和 full-body 权限；
- 触发 fallback。

### 14.3 基本优先级建议

```text
Death / Ragdoll
Hit Reaction
Combat Critical
Interaction Locked
Locomotion Critical
Narrative Full Body
Social Gesture
Idle Expression
```

LLM 的 `Priority` 只能在模板允许的局部范围内调整，不能把挥手升成比受击更高的优先级。

---

## 15. 当前代码的优先修复项

以下任务应在大模型 UMG 之前或并行完成。

# P0：必须先修

### 15.1 Post Process AnimBP 安装方式

当前实现通过 `USkeletalMesh` 设置 Post Process AnimBP，可能影响所有共享该 SkeletalMesh 资产的实例。

目标：

- 优先使用 `USkeletalMeshComponent` 实例级 override；
- 在 UE5.3 本地 API 上验证具体接口；
- 如果目标版本无法安全动态 override，则要求用户显式配置，或为 NPC 使用独立 mesh asset；
- 不允许插件在运行时静默修改共享资产；
- 保存并恢复原有 PostProcess 配置。

验收：

```text
两个角色共享 Manny Mesh
只给其中一个挂 MotionComponent
只有该角色执行插件 PostProcess
```

### 15.2 Runtime LLM 白名单

将以下 direct FK control 默认设为内部模板专用：

```text
right_upperarm.pitch
right_upperarm.yaw
right_upperarm.roll
right_lowerarm.pitch
right_lowerarm.yaw
right_lowerarm.roll
right_hand.pitch
right_hand.yaw
right_hand.roll
```

增加权限枚举：

```cpp
UENUM()
enum class ELLMNPCControlExposure : uint8
{
    RuntimeLLM,
    ApprovedTemplateOnly,
    InternalOnly
};
```

运行时 ActionDecision 永远不能包含这些 control。

### 15.3 Validator 完整化

增加：

- `FMath::IsFinite`；
- Duration 合法；
- track time clamp 到 clip duration；
- `EndTime > StartTime`；
- keyframe 排序；
- 相同时间 key 去重；
- anchor 是否存在；
- target 是否存在；
- required target 是否可解析；
- keyframe 值合法；
- offset 合法；
- track 数量和总 key 数量；
- unsupported track type；
- 空 clip；
- duplicate/conflicting controls；
- channel conflict；
- SkeletonProfile 兼容；
- schema version；
- Published 状态；
- style 参数 clamp；
- 直接 FK 总角度和角速度限制。

建议将接口改成：

```cpp
FLLMMotionCompileResult ValidateAndCompile(
    const FLLMMotionPlan& SourcePlan,
    const FLLMNPCValidationContext& Context,
    FCompiledLLMMotionClip& OutCompiledClip) const;
```

不要只在源结构上原地 Clamp 后直接运行。

### 15.4 Keyframe 与 Envelope 语义

当前 Keyframes 若默认再乘 `Smooth` envelope，可能改变源关键帧曲线。

建议默认：

```text
Keyframes       → None
Hold            → None
Oscillator      → Smooth
Anchor          → EaseInOut
IKReach         → EaseInOut
Spring          → Spring-specific
```

Clip 的整体 BlendIn / BlendOut 仍单独应用。

### 15.5 Target Resolver

新增：

```text
ULLMNPCTargetResolverComponent
```

负责：

- `TargetRef -> Actor`
- 目标是否有效；
- 是否可见；
- 是否已销毁；
- 目标语义点，如 head/chest/object center；
- 转换为 Component Space；
- Reachable 判断；
- 距离和方向。

Sampler 不应在最后一刻静默找不到目标。

### 15.6 Priority 与 Interruptible 真正生效

实现：

- 新动作是否立即抢占；
- Active Clip 是否能被打断；
- 更高系统状态强制打断；
- 同优先级排队；
- Queue 满时的策略；
- ClearQueue 是否停止当前动作；
- 动作取消时平滑返回 Idle。

### 15.7 API 解析与错误

当前 API Client 期待响应 body 直接就是 MotionPlan。后续需要区分：

```text
Proxy canonical response
DeepSeek Chat Completion response
DeepSeek tool call response
Mock response
```

错误信息至少记录：

- HTTP code；
- request id；
- provider error body 的安全摘要；
- finish reason；
- JSON parse error；
- schema error；
- empty response；
- timeout；
- cancellation；
- retry count。

# P1：模板系统阶段修

### 15.8 把 Nod / Wave 从 C++ 测试函数迁入模板库

需要生成：

```text
gesture.nod.neutral.manny.v1.json
gesture.wave.basic.manny.v1.json
```

迁移过程：

1. 复制当前 `BuildNodMotionPlan`；
2. 复制当前通过实机验证的 `BuildWaveMotionPlan`；
3. 补 provenance；
4. 通过 Importer 生成 Runtime Asset；
5. `TestNod` 和 `TestWave` 改成按 TemplateId 执行；
6. 保留硬编码 fallback 一个版本；
7. 验证稳定后删除硬编码 MotionPlan 构造。

Wave 当前包含来自源动作的 local FK 数据，这些轨道应保留为：

```text
ApprovedTemplateOnly
```

而不是运行时 LLM 可生成轨道。

### 15.9 Compiled Track

增加：

```text
FCompiledLLMMotionClip
FCompiledLLMMotionTrack
```

编译阶段完成：

- ControlId 到 OutputSlot；
- bone reference 解析；
- key 排序；
- 时间归一化；
- style binding；
- channel mask；
- target requirement；
- runtime flags；
- template hash。

运行时不再反复通过字符串 `if/else` 解释全部控制项。

### 15.10 Manifest OutputSlot

在 `FLLMControlDefinition` 中增加：

```cpp
UENUM()
enum class ELLMNPCRuntimeOutputSlot : uint8
{
    HeadPitch,
    HeadYaw,
    HeadRoll,
    ChestPitch,
    ChestYaw,
    ChestRoll,
    RightHandIK,
    RightHandOffsetX,
    RightHandOffsetY,
    RightHandOffsetZ,
    RightFingersOpen,
    RightFingersPoint,
    GazeTarget,
    InternalRightUpperArmPitch,
    // ...
};
```

Sampler 使用 `switch(OutputSlot)`，逐步替换长字符串分发。

### 15.11 Spring

当前 `Spring` 若只是返回固定 Amplitude，应：

- 正式实现有状态阻尼弹簧；或
- 在实现前从公开 Schema 中移除；
- 不允许模型选择“看起来存在但实际上没实现”的 TrackType。

# P2：扩展阶段

### 15.12 左侧身体支持

补全：

- left arm IK；
- left direct internal FK；
- left fingers；
- hand auto selection；
- handedness；
- 双手动作通道。

### 15.13 网络

服务器权威选择并复制：

```text
TemplateId
TemplateVersion
TargetNetId
StyleParams
StartServerTime
Seed
```

不复制每帧骨骼姿态。

### 15.14 LOD

距离较远时：

```text
关闭 fingers
降低 IK 更新频率
简化 gaze
停止低优先级 idle gesture
```

---

## 16. Manny 骨骼配置与后续适配

### 16.1 新增 Skeleton Profile

```cpp
UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCSkeletonProfile : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName ProfileId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<USkeleton> Skeleton;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName RootBone;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName PelvisBone;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FName> SpineBones;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName NeckBone;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName HeadBone;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FLLMNPCArmBoneProfile RightArm;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FLLMNPCArmBoneProfile LeftArm;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FLLMNPCFingerBoneProfile RightFingers;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FLLMNPCFingerBoneProfile LeftFingers;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FLLMNPCAxisConvention AxisConvention;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FLLMNPCJointLimit> JointLimits;
};
```

### 16.2 第一版

只发布：

```text
SKP_UE5_Manny
```

必须有自动验证按钮：

```text
Validate Skeleton Profile
```

输出：

- 缺失骨骼；
- 父子链错误；
- finger 链缺失；
- IK 链错误；
- 轴向测试；
- reference pose 信息。

### 16.3 模板兼容等级

```text
ExactSkeleton
ProfileCompatible
Retargeted
Unsupported
```

Direct FK 模板通常要求更严格的 Skeleton/Profile 一致性。IK / semantic 模板更容易跨骨架适配。

### 16.4 后续骨架

顺序建议：

1. UE5 Manny；
2. 与 Manny 骨架高度兼容的角色；
3. MetaHuman 简化社交动作；
4. 自定义 humanoid；
5. 非人形不纳入近期范围。

---

## 17. UEPI + Codex 动作复刻的标准工作流

### 17.1 输入

```text
一个可合法使用的 UAnimSequence
目标 Skeleton Profile
预期动作语义
需要复刻的身体区域
```

### 17.2 Codex 任务模板

```text
1. 调用 uepi_status。
2. 调用 uepi_animation 获取 bone_motion_profile 和 driver_track_curves。
3. 识别 driver_bones、motion_intent_groups 和 source phase。
4. 优先从 local FK driver curves 重建。
5. 根据目标模板类型选择：
   - direct reviewed keyframes
   - oscillator
   - IK anchor
   - hybrid
6. 生成 Draft MotionTemplate JSON。
7. 不自动标记 approved。
8. 运行插件静态 Validator。
9. 在 Manny 测试关卡中预览。
10. 需要时读取 full_pose_artifact 做误差对比。
11. 输出偏差和人工检查项。
```

### 17.3 复刻策略

#### 策略 A：直接压缩 FK 曲线

适合：

- 源动作清晰；
- 骨架一致；
- 追求接近原动画；
- 只在批准模板内部使用。

步骤：

- 选择 driver curves；
- 减少冗余 keys；
- 保留 local transform delta；
- 使用 quaternion/slerp；
- 验证子骨骼传播；
- 与 full pose/end effector 对比。

#### 策略 B：拟合程序算子

适合：

- 点头；
- 摇头；
- 周期性挥手；
- 呼吸；
- 重复拍手；
- 简单摆动。

转换为：

```text
Oscillator
Envelope
Anchor
IKReach
Spring
```

优点：

- 参数化好；
- 适应速度和幅度；
- 模板更小；
- 更适合情绪变化。

#### 策略 C：Hybrid

保留核心 FK / AnimationAsset，再加入程序层：

```text
source motion
+ gaze target
+ chest offset
+ finger pose
+ emotion scale
```

### 17.4 不允许自动发布

Codex 输出：

```text
Draft Template
Test Report
Suggested Review Checklist
```

最终 `Approved` 必须由人工操作。

---

## 18. 人工审核与发布工具

建议新增 Editor 模块：

```text
LLMNPCActionLayerEditor
```

当前 `LLMNPCActionLayerUncooked` 继续只负责 AnimGraph 节点。Editor 模块负责模板生产工具。

### 18.1 Editor 面板

```text
Tools > LLM NPC Action Layer > Template Review
```

功能：

- 选择 Draft JSON；
- 导入；
- Schema 校验；
- 选择 Manny Preview Mesh；
- 播放；
- 循环；
- 调速；
- 显示目标点；
- 显示骨骼；
- 与源 Animation Sequence 对比；
- 显示 end-effector trail；
- 显示 validation warnings；
- Approve；
- Reject；
- Publish；
- 生成 review record。

### 18.2 自动质量指标

至少记录：

```text
Head angular error
Hand end-effector position error
Hand end-effector rotation error
Root displacement
Foot sliding
Joint limit violations
Maximum angular velocity
Maximum angular acceleration
Pose return discontinuity
Missing bone count
Invalid target count
```

不是所有模板都需要和源动画完全相同。指标是辅助人工判断，不是唯一标准。

### 18.3 人工检查清单

- 动作语义是否正确；
- 第一眼是否能识别；
- 肩膀是否自然；
- 肘部是否反折；
- 手腕是否扭曲；
- 手指是否穿插；
- 手是否穿过头部/身体；
- 动作是否平滑进入；
- 动作是否平滑退出；
- 行走时上半身叠加是否稳定；
- 不同帧率下是否稳定；
- 目标变化时是否稳定；
- 失去目标时是否安全；
- 是否允许不同情绪参数；
- 是否含来源或授权风险。

### 18.4 发布产物

```text
Runtime DataAsset
Template Registry Entry
Template Hash
Template Version
Review Record
Source Provenance
Golden Test Result
```

---

## 19. 模板注册表

建议新增：

```text
ULLMNPCMotionTemplateSubsystem : UGameInstanceSubsystem
```

职责：

- 通过 Asset Manager 加载已发布模板；
- 按 TemplateId 查找；
- 按标签筛选；
- Skeleton 兼容筛选；
- 生成给模型的元数据摘要；
- 缓存编译后的 MotionClip；
- 检查版本和 hash；
- 提供 fallback 模板。

### 19.1 不允许同 ID 冲突

启动时检查：

```text
TemplateId 唯一
Version 递增
SkeletonProfile 有效
Published asset 不含 Draft status
AnimationAsset 路径可加载
Style binding 有效
```

### 19.2 Fallback

至少内置：

```text
none
gesture.nod.neutral
```

任何以下情况进入 fallback：

- 模型超时；
- 返回未知 TemplateId；
- 目标失效；
- 模板状态不允许；
- Scheduler 拒绝；
- Skeleton 不兼容；
- style 参数非法；
- 动作资源加载失败。

---

## 20. 情绪、性格与灵动微调

模型不应直接微调骨骼，而应输出统一风格参数。

### 20.1 建议参数

```text
energy
speed
amplitude
openness
tension
symmetry
hesitation
repetition
gaze_strength
hold_duration
```

每个模板声明自己支持哪些参数以及范围。

### 20.2 情绪映射

UE 可先应用确定性基础映射：

```text
friendly:
    openness +0.2
    gaze_strength +0.1

shy:
    amplitude -0.3
    gaze_strength -0.4
    hesitation +0.3

urgent:
    speed +0.25
    energy +0.3
    hold_duration -0.2

sad:
    energy -0.35
    chest posture downward
```

模型只在基础映射上给出有限修正。

### 20.3 Personality

人格参数长期存在：

```text
expressiveness
formality
warmth
confidence
restlessness
```

最终 style：

```text
TemplateDefault
× EmotionMapping
× PersonalityMapping
× LLMBoundedAdjustment
× RuntimeSafetyClamp
```

### 20.4 随机变化

同一模板可通过可复制 Seed 产生轻微变化：

- 开始延迟；
- 幅度；
- 左右选择；
- repetition；
- 手腕 secondary motion。

随机范围必须由模板定义。

---

## 21. Animation Sequence 配置方案

用户可以注册复杂动画资产。

### 21.1 动画资产模板

```json
{
  "template_id": "locomotion.walk.forward.manny.v1",
  "execution_mode": "animation_asset",
  "animation_asset": "/Game/Characters/Mannequins/Animations/MM_Walk_Fwd.MM_Walk_Fwd",
  "playback": {
    "loop": true,
    "rate_min": 0.8,
    "rate_max": 1.2,
    "root_motion_policy": "engine_controlled"
  },
  "constraints": {
    "channels": ["full_body", "locomotion"],
    "requires_nav_request": true
  }
}
```

### 21.2 不让 LLM直接播放任意资产路径

模型只能选择注册表中的：

```text
TemplateId
```

不能返回：

```text
/Game/Anything/UnknownAsset
```

### 21.3 Locomotion 与动画播放分离

对于“走到玩家旁边”：

```text
模型输出 LocomotionIntent
UE AIController MoveTo
AnimBP 根据速度自动进入 Walk
到达后 Scheduler 执行 gesture
```

模型不需要选每个走路动画。

### 21.4 Complex Action 的长期策略

不要求所有 AnimationAsset 最终都程序化。保留 Hybrid 是合理的正式能力，而不是临时补丁。

---

## 22. 模块划分建议

### 22.1 Runtime 模块

`LLMNPCActionLayer`

```text
Motion/
  MotionTypes
  MotionComponent
  MotionValidator
  MotionCompiler
  MotionSampler
  MotionTemplate
  MotionTemplateSubsystem
  SkeletonProfile

Decision/
  ActionDecisionTypes
  ActionDecisionValidator
  ContextBuilder
  TemplateQueryService
  ActionScheduler
  TargetResolver

Provider/
  ModelProvider
  DeepSeekProvider
  ProxyProvider
  MockProvider

Conversation/
  ConversationComponent
  ConversationHistory
  NPCContext

Animation/
  AnimNode_LLMProceduralPose
  PostProcessAnimInstance
  AnimationAssetExecutor
  HybridExecutor

Locomotion/
  LocomotionCoordinator

Debug/
  DebugState
  DebugEvents
```

依赖建议：

```text
Core
CoreUObject
Engine
HTTP
Json
JsonUtilities
DeveloperSettings
AnimGraphRuntime
AnimationCore
GameplayTags
UMG
Slate
SlateCore
AIModule
NavigationSystem（到 locomotion 阶段再加入）
```

### 22.2 UncookedOnly 模块

`LLMNPCActionLayerUncooked`

只放：

```text
UAnimGraphNode_LLMProceduralPose
AnimGraph 编辑器展示代码
```

### 22.3 Editor 模块

新增：

```text
LLMNPCActionLayerEditor
```

放：

```text
Template JSON Importer
Template Factory
Template Review Panel
Template Publisher
UEPI Artifact Importer（可选）
Pose Comparison
Validation Commandlet
Skeleton Profile Wizard
```

### 22.4 外部 Backend 示例

```text
Services/
  llmnpc_proxy/
```

职责：

- 保管 DeepSeek Key；
- 调 DeepSeek；
- 管理多轮消息；
- 进行模板语义检索；
- 将厂商响应转换为 canonical ActionDecision；
- 限流；
- 日志；
- 重试；
- 缓存。

---

## 23. 建议仓库目录

```text
LLMNPCActionLayer/
├─ LLMNPCActionLayer.uplugin
├─ README.md
├─ developv2.md
├─ StepImprove.md
│
├─ Source/
│  ├─ LLMNPCActionLayer/
│  │  ├─ Public/
│  │  └─ Private/
│  ├─ LLMNPCActionLayerUncooked/
│  └─ LLMNPCActionLayerEditor/
│
├─ Content/
│  └─ LLMNPCActionLayer/
│     ├─ Animation/
│     ├─ UI/
│     ├─ Templates/
│     ├─ SkeletonProfiles/
│     ├─ Data/
│     ├─ Debug/
│     └─ Maps/
│
├─ TemplateSources/
│  ├─ Schemas/
│  │  ├─ motion-template-v1.schema.json
│  │  └─ action-decision-v1.schema.json
│  └─ Manny/
│     ├─ Draft/
│     ├─ Approved/
│     └─ Deprecated/
│
├─ Tests/
│  ├─ Json/
│  ├─ Golden/
│  └─ Automation/
│
├─ Services/
│  └─ llmnpc_proxy/
│
└─ Docs/
   ├─ TemplateAuthoring.md
   ├─ UEPIWorkflow.md
   ├─ DeepSeekProvider.md
   ├─ SkeletonProfiles.md
   └─ RuntimeSecurity.md
```

---

## 24. 完整阶段路线

以下阶段按依赖关系排列，不代表必须一次全部完成。

# Step 0：冻结当前基线

目标：

- 标记当前可运行 commit；
- 保存 UE5.3 构建日志；
- 保存 Nod / Wave 实机视频或截图；
- 保存当前 JSON 样例；
- 建立回归标准。

产物：

```text
baseline-v0.2.x tag
KnownIssues.md
Nod baseline
Wave baseline
```

验收：

- 可以从干净 UE5.3 第三人称项目编译；
- 能复现点头和挥手；
- 记录安装步骤。

# Step 1：Runtime 硬化

实现第 15 节 P0 项。

重点：

- PostProcess 组件级安装；
- Validator；
- target；
- interrupt；
- LLM-safe control；
- keyframe/envelope；
- API 错误。

验收：

- 未知 control 被拒绝；
- NaN/Infinity 被拒绝；
- 不存在 target 被明确拒绝；
- 共享 Manny Mesh 不被全局修改；
- active motion 可安全取消；
- direct FK 不可由 Runtime LLM 路径提交。

建议版本：

```text
0.2.1 / 0.2.2
```

# Step 2：动作模板库 V1

目标：

- Authoring JSON；
- Runtime DataAsset；
- Registry；
- Importer；
- Schema；
- Nod / Wave 迁移。

验收：

```text
TestNod → ExecuteTemplate("gesture.nod.neutral.manny.v1")
TestWave → ExecuteTemplate("gesture.wave.basic.manny.v1")
```

不再依赖手写 BuildNod / BuildWave 作为主路径。

建议版本：

```text
0.3.0
```

# Step 3：Mock 对话与 UMG

目标：

- WBP Chat；
- Conversation Component；
- Mock Provider；
- ActionDecision；
- Candidate query；
- Scheduler。

验收：

```text
输入“挥手” → 文本回复 + wave
输入“点头” → 文本回复 + nod
输入“不要动作” → 只有文本
返回未知 ID → fallback
```

建议版本：

```text
0.3.x
```

# Step 4：DeepSeek Provider

目标：

- Provider 抽象；
- DeepSeek Chat Completions；
- canonical parser；
- JSON Output；
- 可选 strict tool call；
- conversation history；
- proxy mode；
- direct editor mode。

验收：

- API 超时不崩溃；
- 空 content 有 fallback；
- finish reason `length` 不执行残缺动作；
- 返回模板必须来自候选；
- API Key 不进入 Shipping；
- 模型名可配置。

建议版本：

```text
0.4.0
```

# Step 5：UEPI + Codex 动作生产工作流

目标：

- 写 `Docs/UEPIWorkflow.md`；
- 定义 UEPI artifact → Draft Template 规则；
- 建立 Codex prompt；
- 导入一个新的动画；
- 生成草稿；
- 自动检查；
- 实机审核；
- 发布。

首个验证动作建议：

```text
摇头
招手
指向
```

每次只选一个动作完整走通生产线。

验收：

- Codex 能从 `driver_track_curves` 生成模板；
- full pose 只用于验证；
- 模板保留 provenance；
- 未人工审核不能进入 Registry；
- 审核后的动作可由 TemplateId 执行。

建议版本：

```text
0.5.0
```

# Step 6：自然对话下的自主动作选择

目标：

从“命令动作”升级为：

```text
普通对话
+ 情绪
+ 性格
+ 关系
+ 身体状态
+ 场景目标
→ 自主选择动作
```

重点：

- 模板元数据；
- candidate filtering；
- no-action 行为；
- cooldown；
- 动作频率限制；
- 动作和对白语义一致性。

验收场景：

```text
玩家问候 → friendly wave 或 nod
玩家表示感谢 → nod
NPC 不知道答案 → shrug（模板存在时）
NPC 指路 → point target
连续普通问答 → 不会每句重复挥手
右手被占用 → 不选右手动作
```

建议版本：

```text
0.6.0
```

# Step 7：情绪与性格风格层

目标：

- style parameter；
- emotion map；
- personality map；
- template parameter binding；
- deterministic seed；
- 多种 wave/nod 变体。

验收：

同一 wave 在以下角色上有可见但受限的差异：

```text
friendly
shy
urgent
formal
```

并且不会超过关节和模板范围。

建议版本：

```text
0.7.0
```

# Step 8：Skeleton Profile

目标：

- Manny Profile 正式资产化；
- 自动验证；
- AnimNode 不再完全依赖构造函数硬编码；
- 模板兼容等级；
- 第二骨架试验。

验收：

- Manny Profile 缺骨骼时给出明确错误；
- 模板知道自己是否 ExactSkeleton；
- 不兼容骨架不会执行 direct FK 模板；
- IK/semantic 模板有可控降级。

建议版本：

```text
0.8.0
```

# Step 9：AnimationAsset / Hybrid

目标：

- 用户注册 Sequence / Montage；
- 白名单 TemplateId；
- AnimationAsset Executor；
- Hybrid overlay；
- full-body channel；
- root motion policy。

验收：

- 模型不能返回任意资产路径；
- 已注册复杂动画可播放；
- 播放时社交 upper-body 动作不会错误叠加；
- Hybrid 可增加 gaze；
- 受击/死亡可强制中断。

建议版本：

```text
0.9.0
```

# Step 10：LocomotionIntent

目标：

- 模型只输出目标和 gait；
- UE AIController/NavMesh 执行；
- 到达后动作；
- 移动与上半身 gesture 协同；
- 失败反馈。

验收：

```text
“走到我这里” → MoveTo(player)
到达 → 停止 + 看向
路径不存在 → 安全失败
战斗状态 → UE 拒绝
```

# Step 11：生产级完善

内容：

- Multiplayer；
- LOD；
- analytics；
- request cancellation；
- cache；
- localization；
- content packaging；
- documentation；
- CI；
- UE5.3 clean project test；
- 后续 UE 版本兼容测试。

---

## 25. 测试体系

### 25.1 C++ Automation Tests

覆盖：

```text
JSON parse
Schema version
Unknown template
Unknown control
Track time clamp
Key sorting
Duplicate keys
NaN / Infinity
Invalid anchor
Invalid target
Conflict channels
Style clamp
Template hash
Skeleton profile
Scheduler preemption
Provider timeout
Provider cancellation
```

### 25.2 Golden Motion Tests

对 Nod / Wave 保存 Golden 指标：

```text
关键 normalized_time 下的 Snapshot
关键骨骼 transform
hand end-effector
head rotation
clip completion
return-to-idle
```

避免每次改 AnimNode 后动作悄悄变形。

### 25.3 PIE Functional Test

测试地图：

```text
M_LLMNPCActionLayer_Test
```

场景：

- Manny NPC；
- player；
- target cube；
- chat UMG；
- debug panel；
- shared mesh NPC；
- moving NPC；
- right-hand-busy NPC。

### 25.4 Provider Contract Test

后端或 mock 返回：

- 正常 JSON；
- 空；
- 非 JSON；
- 截断；
- unknown template；
- illegal style；
- target missing；
- `action: null`；
- HTTP 429；
- HTTP 500；
- timeout。

---

## 26. Debug 与可观察性

建议 Debug State 增加：

```text
ConversationId
RequestId
ProviderName
ModelName
RequestLatency
CandidateTemplateIds
RawProviderResponse（仅开发）
ParsedDecision
DecisionValidationError
SelectedTemplateId
TemplateVersion
SchedulerResult
ActiveChannels
ActiveTime
ResolvedTarget
Snapshot
FallbackReason
```

UMG Debug 模式显示：

```text
User Text
NPC Text
Candidates
Model Choice
Validation Result
Current Template
Current Target
Current Style
Queue
```

Shipping 构建默认关闭敏感 raw log。

---

## 27. 安全、隐私与成本

### 27.1 API 安全

- Shipping 不嵌入 API Key；
- 后端代理；
- HTTPS；
- request size limit；
- response size limit；
- timeout；
- rate limit；
- log 去隐私；
- `user_id` 不包含隐私信息；
- 禁止模型返回可执行代码。

### 27.2 动作安全

- Runtime 只执行 Published template；
- TemplateId 白名单；
- TargetRef 白名单；
- style clamp；
- Skeleton joint limit；
- channel rule；
- gameplay state gate；
- no root direct control；
- no physics impulse；
- no arbitrary asset path；
- no runtime MCP。

### 27.3 成本控制

- 不每帧调用模型；
- 不为动作进行单独第二次请求，第一版在同一个聊天返回中选动作；
- 高频简单命令可以本地匹配；
- 候选 Top-K；
- 对话摘要；
- 重复场景缓存；
- NPC 没有可用动作时不发送动作候选；
- raw bone data 不发给运行时模型。

---

## 28. 网络与确定性

多人模式后续原则：

```text
Server:
    对话决定
    Validator
    Scheduler
    Template execution authority

Clients:
    接收 TemplateId / Params / StartTime / Seed
    本地采样同一动作
```

复制结构：

```cpp
USTRUCT()
struct FReplicatedLLMNPCAction
{
    GENERATED_BODY()

    FName TemplateId;
    int32 TemplateVersion;
    FString TargetNetRef;
    TArray<FLLMNPCStyleValue> Style;
    float ServerStartTime;
    int32 Seed;
};
```

不要复制所有骨骼 Snapshot。

---

## 29. 对 7 点设想的逐条落实

### 29.1 DeepSeek + UMG

认可。第一版只做文字交流和明确命令动作。

补充：

- 先用 Mock Provider 打通 UI；
- 再接 DeepSeek；
- Provider 抽象；
- 正式环境使用后端；
- ActionDecision 不直接输出 MotionClip；
- 允许 `action: null`。

### 29.2 Codex + MCP 获取动画数据并沉淀模板

认可。这应成为正式的离线 Authoring Pipeline。

补充：

- 优先使用 UEPI `driver_track_curves`；
- full pose 用于验证；
- 生成 Draft；
- 自动检查；
- 人工审核；
- Published 才进入运行时；
- 记录 provenance 和 hash；
- 注意动画来源授权。

### 29.3 运行时自发选择模板并微调

认可。

补充：

- UE 先筛候选；
- 模型只在候选中选；
- 第一阶段不是向量 RAG，而是 metadata registry；
- 微调通过 bounded style params；
- 不允许微调 raw bones；
- 加动作频率和 no-action 策略。

### 29.4 Manny 先行

认可。

补充：

- 现在就设计 Skeleton Profile 接口；
- 第一阶段只实现 Manny；
- direct FK 模板标记 ExactSkeleton；
- 后续骨架通过 Profile 和兼容等级进入。

### 29.5 修复当前问题并迁移 Nod / Wave

认可，且应是近期最高优先级。

补充：

- 修 PostProcess；
- direct FK 权限；
- Validator；
- Scheduler；
- target；
- 将 BuildNod / BuildWave 迁移成模板；
- 保留 Golden Tests。

### 29.6 Runtime 模型只选择，外部 Codex 负责动作工程

强烈认可。这应写成安全边界，不只是当前实现习惯。

补充：

```text
Runtime DeepSeek = Planner / Selector
External Codex = Authoring Engineer
UE = Executor / Validator
Human = Publisher
```

### 29.7 用户配置复杂 Animation Sequence

认可，而且建议将 AnimationAsset / Hybrid 作为长期正式能力。

补充：

- 只注册白名单；
- LLM 选 TemplateId；
- locomotion 由 UE 状态机和 AIController；
- 不强求所有复杂动作最终程序化；
- 对高价值动作再通过 UEPI/Codex 转换。

---

## 30. 当前最近一轮建议执行顺序

从当前仓库出发，建议下一轮按以下顺序实际编码：

```text
1. 修复 PostProcess 实例安装问题
2. 为 direct FK controls 增加 exposure 权限
3. 扩充 MotionValidator
4. 实现 TargetResolver
5. 实现 Scheduler 的 priority / interrupt / channel
6. 定义 MotionTemplate JSON Schema
7. 新建 ULLMNPCMotionTemplate 和 TemplateSubsystem
8. 将 Nod 迁移成第一个模板
9. 将 Wave 迁移成第二个模板
10. 新建 Mock Provider
11. 新建 Conversation Component
12. 新建 UMG Chat
13. 定义 ActionDecision Schema
14. 接 DeepSeek Provider
15. 写 UEPI/Codex Authoring 文档
16. 从第三个真实动画开始验证自动生产流水线
```

在第 8 和第 9 项完成前，不建议继续增加更多硬编码动作。

---

## 31. 第一阶段 Definition of Done

第一阶段可认为完成，需要同时满足：

- UE5.3 第三人称模板干净项目可安装和编译；
- NPC 只挂组件和指定配置即可工作；
- 不修改共享 SkeletalMesh 资产；
- Nod / Wave 来自模板资产，不是主路径硬编码；
- Runtime 模型不能提交 direct bone tracks；
- Template Registry 只包含 Published；
- UMG 能文字对话；
- DeepSeek 能返回 canonical ActionDecision；
- 模型只能选择本次候选模板；
- 未知模板有 fallback；
- 对话可以选择不动作；
- TargetRef 严格验证；
- action priority / interrupt 生效；
- API Key 不进入 Shipping；
- UEPI/Codex 可以生成一个新 Draft 模板；
- 新模板经过人工审核后才能发布；
- 有至少一张自动测试地图；
- 有至少一组 Golden Motion Test；
- README 有完整安装、测试和安全边界说明。

---

## 32. 暂不纳入近期范围

为控制复杂度，近期不做：

- 运行时大模型生成任意骨骼动画；
- 运行时调用 MCP / Codex；
- 无审核自动发布动作模板；
- 通用非人形骨架；
- 完整舞蹈生成；
- 高质量战斗动画生成；
- 多人肢体接触自动生成；
- 运行时重新训练模型；
- 将所有 Animation Sequence 强制转成程序动画；
- LLM 逐帧控制 locomotion；
- 在客户端存储生产 API Key。

---

## 33. 参考资料

### UEProjectIntelligence

- Repository: <https://github.com/Nath-Vikky/UEProjectIntelligence>
- Read workflow: `Docs/read-workflows.md`
- Sample MCP calls: `Docs/sample-queries.md`
- Animation reader: `Source/UEProjectIntelligence/Private/Animation/UEPIAnimationReader.cpp`
- MCP query path: `Services/uepi/src/uepi/query.py`
- Animation reconstruction commit: `102a59f67293a621f41b9b98ac3b312875ae1e37`

### LLMNPCActionLayer

- Repository: <https://github.com/Nath-Vikky/LLMNPCActionLayer>
- Current V2 flow: `README.md`
- Motion data: `LLMNPCMotionTypes.h`
- Motion runtime: `LLMNPCMotionComponent`
- Validator: `LLMNPCMotionValidator`
- Sampler: `LLMNPCMotionSampler`
- Pose node: `FAnimNode_LLMProceduralPose`

### DeepSeek

- API quick start: <https://api-docs.deepseek.com/>
- Chat Completions: <https://api-docs.deepseek.com/api/create-chat-completion/>
- JSON Output: <https://api-docs.deepseek.com/guides/json_mode/>
- Tool Calls: <https://api-docs.deepseek.com/guides/tool_calls/>
- Multi-round Conversation: <https://api-docs.deepseek.com/guides/multi_round_chat/>

---

## 34. 最终产品定义

`LLMNPCActionLayer` 的最终定位应是：

> 一个面向 Unreal Engine 的受约束 NPC 身体行为层。它允许运行时语言模型根据对话、情绪、性格、关系、场景和 Gameplay 状态选择经过审核的动作模板，并在模板允许范围内生成风格参数；UE 使用程序化动画、IK、动画资产、状态机、寻路和物理系统执行动作。离线动作模板由 UEPI、Codex、程序工具和人工审核共同生产，运行时模型无法访问或修改底层骨骼数据。

最终系统闭环：

```text
Animation Knowledge
    ↓ UEPI / Codex
Approved Motion Template Library
    ↓ metadata retrieval
DeepSeek Action Decision
    ↓ strict UE validation
Scheduler / Target Resolver
    ↓
Procedural / Asset / Hybrid / Locomotion Execution
    ↓
Observable, reviewable, reproducible NPC body behavior
```
