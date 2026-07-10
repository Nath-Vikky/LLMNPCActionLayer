#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Context/LLMNPCContextTypes.h"
#include "LLMNPCRelationshipComponent.generated.h"

UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class LLMNPCACTIONLAYER_API ULLMNPCRelationshipComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="LLM NPC|Context|Relationship")
	void SetRelationship(
		const FString& InOtherActorRef,
		float InFamiliarity,
		float InTrust,
		float InAffinity,
		const TArray<FName>& InTags
	);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Context|Relationship")
	FLLMNPCRelationshipSnapshot GetRelationshipSnapshot() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Relationship")
	FString OtherActorRef;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Relationship", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Familiarity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Relationship", meta=(ClampMin="-1.0", ClampMax="1.0"))
	float Trust = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Relationship", meta=(ClampMin="-1.0", ClampMax="1.0"))
	float Affinity = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LLM NPC|Context|Relationship")
	TArray<FName> RelationshipTags;
};
