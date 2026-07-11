#pragma once

#include "CoreMinimal.h"
#include "LLMNPCStyleTypes.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCStylePreset
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	FName StyleTag = TEXT("neutral");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	float AmplitudeScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	float SpeedScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	float DurationScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	float FrequencyScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	float OffsetScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	float BlendTimeScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	float MaxPhaseJitterRadians = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	float MicroMotionScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Style")
	float GazeEngagement = 0.5f;
};
