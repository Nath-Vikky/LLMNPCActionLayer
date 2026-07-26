#pragma once

#include "CoreMinimal.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "LLMNPCResolvedMotionModifiers.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCTemplateModifiersV2
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	FLLMNPCTemplateModifiers Requested;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	float ReachScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	float HeightScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	float LateralScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	int32 CycleCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	float GazeEngagement = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	float PalmOrientationWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	float FingerPoseWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	float TorsoParticipation = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	float BlendInScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Modifiers")
	float BlendOutScale = 1.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCResolvedMotionModifiers
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float Amplitude = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float SpeedScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float DurationScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float ReachScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float HeightScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float LateralScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	int32 CycleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float GazeEngagement = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float PalmOrientationWeight = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float FingerPoseWeight = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float TorsoParticipation = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float BlendInScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	float BlendOutScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	bool bMirror = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	FString TargetRef;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	FName Style = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	int32 RandomSeed = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	bool bNeedsFallbackSelection = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Modifiers")
	FName ResultCode = TEXT("LLMNPC_MODIFIER_RESOLVED");

	FLLMNPCTemplateModifiers ToLegacyModifiers() const
	{
		FLLMNPCTemplateModifiers Result;
		Result.TargetRef = TargetRef;
		Result.Amplitude = Amplitude;
		Result.SpeedScale = SpeedScale;
		Result.DurationScale = DurationScale;
		Result.Style = Style;
		Result.bMirror = bMirror;
		Result.RandomSeed = RandomSeed;
		return Result;
	}
};

USTRUCT(BlueprintType)
struct FLLMNPCModifierResolutionStep
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	FName Stage = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	FName Modifier = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	FName Operation = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	float Before = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	float Contribution = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	float After = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	FString Reason;
};

USTRUCT(BlueprintType)
struct FLLMNPCModifierResolutionTrace
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	FString SchemaVersion = TEXT("llmnpc.modifier_resolution_trace.v1");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	TArray<FLLMNPCModifierResolutionStep> Steps;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	FName ResultCode = TEXT("LLMNPC_MODIFIER_RESOLVED");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Context|Trace")
	bool bNeedsFallbackSelection = false;

	FString ToSummary() const;
};
