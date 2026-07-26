#pragma once

#include "CoreMinimal.h"
#include "LLMNPCExecutionContextTypes.generated.h"

UENUM(BlueprintType)
enum class ELLMNPCExecutionMovementMode : uint8
{
	Stationary,
	Walking,
	Running,
	Turning,
	Falling,
	Other
};

UENUM(BlueprintType)
enum class ELLMNPCTargetLossPolicy : uint8
{
	CancelMotion,
	HoldLast,
	FadeOut
};

USTRUCT(BlueprintType)
struct FLLMNPCObstacleSweepResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	bool bTested = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	bool bBlockingHit = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	float Clearance = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FVector ImpactNormalCS = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FLLMNPCTargetExecutionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FString TargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FVector LocationCS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FVector DirectionCS = FVector::ForwardVector;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FVector VelocityCS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	float DistanceCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	float HeightRelativeCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	bool bTeleported = false;
};

USTRUCT(BlueprintType)
struct FLLMNPCExecutionContextSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FLLMNPCTargetExecutionSnapshot Target;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FVector OwnerVelocityCS = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	float OwnerSpeedCmPerSecond = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	ELLMNPCExecutionMovementMode MovementMode =
		ELLMNPCExecutionMovementMode::Stationary;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	bool bRightHandOccupied = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	bool bLeftHandOccupied = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	bool bUpperBodyOccupied = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	float AvailableSpace = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FLLMNPCObstacleSweepResult RightObstacle;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FLLMNPCObstacleSweepResult LeftObstacle;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	int32 CurrentLOD = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Execution")
	FName BasePoseTag = TEXT("idle");

	bool IsFinite() const
	{
		const auto IsFiniteVector = [](const FVector& Value)
		{
			return
				FMath::IsFinite(Value.X) &&
				FMath::IsFinite(Value.Y) &&
				FMath::IsFinite(Value.Z);
		};
		return
			IsFiniteVector(OwnerVelocityCS) &&
			FMath::IsFinite(OwnerSpeedCmPerSecond) &&
			FMath::IsFinite(AvailableSpace) &&
			IsFiniteVector(Target.LocationCS) &&
			IsFiniteVector(Target.DirectionCS) &&
			IsFiniteVector(Target.VelocityCS) &&
			FMath::IsFinite(Target.DistanceCm) &&
			FMath::IsFinite(Target.HeightRelativeCm) &&
			FMath::IsFinite(RightObstacle.Clearance) &&
			FMath::IsFinite(LeftObstacle.Clearance) &&
			IsFiniteVector(RightObstacle.ImpactNormalCS) &&
			IsFiniteVector(LeftObstacle.ImpactNormalCS);
	}
};

struct FLLMNPCTargetRuntimeSample
{
	FVector LocationWS = FVector::ZeroVector;
	FVector LastObservedLocationWS = FVector::ZeroVector;
	float Alpha = 0.0f;
	bool bValid = false;
	bool bTeleported = false;
	bool bHasObservedLocation = false;
};
