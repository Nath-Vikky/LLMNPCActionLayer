#pragma once

#include "CoreMinimal.h"
#include "Context/LLMNPCContextTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Style/LLMNPCStyleTypes.h"
#include "LLMNPCStyleResolver.generated.h"

UCLASS()
class LLMNPCACTIONLAYER_API ULLMNPCStyleResolver : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="LLM NPC|Style")
	static FName ResolveRecommendedStyle(
		const FLLMNPCSelectionContextSnapshot& Context,
		const TArray<FName>& AllowedStyles
	);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Style")
	static FLLMNPCStylePreset GetBuiltInPreset(FName StyleTag);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Style")
	static int32 BuildDeterministicSeed(
		const FGuid& SessionId,
		const FGuid& RequestId,
		FName NPCId,
		FName SelectionId
	);
};
