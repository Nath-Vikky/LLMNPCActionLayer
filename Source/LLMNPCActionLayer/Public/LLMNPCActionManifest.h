#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "LLMNPCActionManifest.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCActionTemplate
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action")
	FString ActionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action", meta=(ClampMin="0.0"))
	float MinDuration = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action", meta=(ClampMin="0.0"))
	float MaxDuration = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinAmplitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaxAmplitude = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action")
	bool bRequiresTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action")
	bool bRequiresUpperBodyFree = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action")
	bool bCanRunWhileMoving = true;
};

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCActionManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC Action")
	TArray<FLLMNPCActionTemplate> Templates;

	const FLLMNPCActionTemplate* FindTemplateById(const FString& ActionId) const;
};
