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
};

class LLMNPCACTIONLAYER_API FLLMNPCTemplateCompiler
{
public:
	static bool Compile(
		const ULLMNPCMotionTemplate& MotionTemplate,
		const FLLMNPCTemplateModifiers& Modifiers,
		const ULLMNPCSkeletonProfile& SkeletonProfile,
		FLLMMotionPlan& OutPlan,
		FString& OutError
	);
};
