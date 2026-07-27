#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionTypes.h"
#include "LLMNPCMotionValidator.generated.h"

USTRUCT(BlueprintType)
struct FLLMMotionValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion")
	bool bValid = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC Motion")
	FString ErrorMessage;
};

UENUM(BlueprintType)
enum class ELLMNPCMotionValidationSource : uint8
{
	RuntimeModel,
	PublishedTemplate,
	AuthoringSandbox,
	ReplicatedAuthority,
	InternalDebug
};

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCMotionValidator : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<const ULLMNPCControlManifest> Manifest;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="0.05", ClampMax="3.0"))
	float MaxClipDuration = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="1", ClampMax="32"))
	int32 MaxTracks = 24;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC Motion", meta=(ClampMin="1", ClampMax="16"))
	int32 MaxFloatKeysPerTrack = 12;

	FLLMMotionValidationResult ValidateAndClamp(
		FLLMMotionPlan& InOutPlan,
		ELLMNPCMotionValidationSource Source = ELLMNPCMotionValidationSource::RuntimeModel
	) const;

private:
	bool ValidateTrack(
		FLLMMotionTrack& InOutTrack,
		float ClipDuration,
		ELLMNPCMotionValidationSource Source,
		FString& OutError
	) const;
	const FLLMControlDefinition* FindControl(FName ControlId) const;
};
