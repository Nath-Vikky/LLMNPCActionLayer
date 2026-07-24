#pragma once

#include "CoreMinimal.h"
#include "LLMNPCMotionTypes.h"
#include "LLMNPCTemplateCompiler.generated.h"

class ULLMNPCMotionTemplate;
class ULLMNPCSkeletonProfile;

USTRUCT(BlueprintType)
struct FLLMNPCTemplateModifiers
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Template")
	FString TargetRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Template")
	float Amplitude = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Template")
	float SpeedScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Template")
	float DurationScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Template")
	FName Style = TEXT("neutral");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Template")
	bool bMirror = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Template")
	int32 RandomSeed = 0;

	FVector2D ContextAmplitudeRange = FVector2D::ZeroVector;
	FVector2D ContextSpeedRange = FVector2D::ZeroVector;
	FVector2D ContextDurationRange = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct FLLMNPCTemplateResolvedModifiers
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template")
	FString TargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template")
	float Amplitude = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template")
	float SpeedScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template")
	float DurationScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template")
	FName Style = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template")
	bool bMirror = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Template")
	int32 RandomSeed = 0;
};

class LLMNPCACTIONLAYER_API FLLMNPCTemplateCompiler
{
public:
	static bool Compile(
		const ULLMNPCMotionTemplate& MotionTemplate,
		const FLLMNPCTemplateModifiers& Modifiers,
		const ULLMNPCSkeletonProfile& SkeletonProfile,
		FLLMMotionPlan& OutPlan,
		FString& OutError,
		FLLMNPCTemplateResolvedModifiers* OutResolvedModifiers = nullptr
	);
};
