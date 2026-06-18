#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LLMNPCActionManifest.h"
#include "LLMNPCActionTypes.h"
#include "LLMNPCActionValidator.generated.h"

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCActionValidator : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="LLM NPC Action|Validation")
	bool ValidateAndClamp(
		UPARAM(ref) FLLMNPCActionRequest& Action,
		const ULLMNPCActionManifest* Manifest,
		FString& OutReason
	) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC Action|Validation")
	bool IsAllowedActionId(const FString& ActionId, const ULLMNPCActionManifest* Manifest) const;

	static bool IsTargetRequired(const FString& ActionId, const ULLMNPCActionManifest* Manifest);

private:
	static const FLLMNPCActionTemplate* FindBuiltInTemplate(const FString& ActionId);
};
