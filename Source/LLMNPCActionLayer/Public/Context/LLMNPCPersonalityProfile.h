#pragma once

#include "CoreMinimal.h"
#include "Context/LLMNPCContextTypes.h"
#include "Engine/DataAsset.h"
#include "LLMNPCPersonalityProfile.generated.h"

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCPersonalityProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="LLM NPC|Context|Personality")
	FLLMNPCPersonalitySnapshot GetPersonalitySnapshot() const
	{
		FLLMNPCPersonalitySnapshot Snapshot;
		Snapshot.ProfileId = ProfileId;
		Snapshot.Expressiveness = FMath::Clamp(Expressiveness, 0.25f, 1.5f);
		Snapshot.Shyness = FMath::Clamp(Shyness, 0.0f, 1.0f);
		Snapshot.Sociability = FMath::Clamp(Sociability, 0.0f, 1.0f);
		Snapshot.PersonalityTags = PersonalityTags;
		return Snapshot;
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Personality")
	FName ProfileId = TEXT("neutral");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Personality", meta=(ClampMin="0.25", ClampMax="1.5"))
	float Expressiveness = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Personality", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Shyness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Personality", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Sociability = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Context|Personality")
	TArray<FName> PersonalityTags;
};
