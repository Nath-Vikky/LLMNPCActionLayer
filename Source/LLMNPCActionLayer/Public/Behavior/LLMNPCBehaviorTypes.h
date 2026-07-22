#pragma once

#include "CoreMinimal.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "LLMNPCBehaviorTypes.generated.h"

UENUM(BlueprintType)
enum class ELLMNPCBehaviorStepKind : uint8
{
	MoveToTarget,
	FaceTarget,
	PlayTemplate,
	Wait
};

UENUM(BlueprintType)
enum class ELLMNPCBehaviorState : uint8
{
	Idle,
	Validating,
	Moving,
	Facing,
	Waiting,
	ExecutingAction,
	Completed,
	Failed,
	Cancelled
};

USTRUCT(BlueprintType)
struct FLLMNPCBehaviorStep
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	ELLMNPCBehaviorStepKind Kind = ELLMNPCBehaviorStepKind::Wait;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FString TargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	float AcceptanceRadiusCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FName TemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FLLMNPCTemplateModifiers TemplateModifiers;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	float DurationSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	float TimeoutSeconds = 0.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCBehaviorPlan
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FGuid PlanId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	TArray<FLLMNPCBehaviorStep> Steps;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	float TimeoutSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	bool bInterruptible = true;
};

USTRUCT(BlueprintType)
struct FLLMNPCBehaviorPolicy
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	bool bNavigationEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	bool bSpawnDefaultAIController = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	float DefaultAcceptanceRadiusCm = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	float MinAcceptanceRadiusCm = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	float MaxAcceptanceRadiusCm = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	float PlanTimeoutSeconds = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	float MoveTimeoutSeconds = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	float FaceTimeoutSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	float FacingTurnRateDegreesPerSecond = 360.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	float FacingToleranceDegrees = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Behavior")
	float TickIntervalSeconds = 0.05f;
};

USTRUCT(BlueprintType)
struct FLLMNPCBehaviorExecutionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	bool bAccepted = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	bool bBehaviorStarted = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	bool bActionExecuted = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FGuid BehaviorPlanId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FName ResolvedTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	ELLMNPCBehaviorState State = ELLMNPCBehaviorState::Idle;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FName ErrorCode = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior")
	FString ErrorMessage;
};

USTRUCT(BlueprintType)
struct FLLMNPCBehaviorDebugState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	ELLMNPCBehaviorState State = ELLMNPCBehaviorState::Idle;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	FGuid ActivePlanId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	int32 ActiveStepIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	int32 StepCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	ELLMNPCBehaviorStepKind ActiveStepKind = ELLMNPCBehaviorStepKind::Wait;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	FString ActiveTargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	FName ActiveTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	bool bActionExecuted = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	FName LastErrorCode = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Behavior|Debug")
	FString LastErrorMessage;
};
