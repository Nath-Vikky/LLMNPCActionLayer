#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/LLMNPCTemplateCatalogTypes.h"
#include "LLMNPCPublicActionDefinition.generated.h"

class ULLMNPCActionVocabulary;

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCPublicActionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	FName PublicActionId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	FString SemanticVersion = TEXT("1.0.0");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action", meta=(ClampMin="1"))
	int32 DefinitionRevision = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action", meta=(MultiLine="true"))
	FString SelectionSummary;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	TArray<FString> SuitableWhen;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	TArray<FString> AvoidWhen;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	TArray<FName> SemanticEffectTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	TArray<FName> TargetCategoryTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	FName GestureFamily = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	FName DefaultStyle = TEXT("neutral");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	TArray<FName> IncompatibleActionFamilies;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	TArray<FString> SearchKeywords;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	bool bRequiresTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	FString CatalogSchemaVersion = LLMNPCCatalog::PublicActionSchemaVersion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	ELLMNPCTemplateReviewState ReviewState = ELLMNPCTemplateReviewState::Draft;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action")
	FString ContentHash;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Public Action", meta=(MultiLine="true"))
	FString ReviewRecordJson;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Public Action")
	bool IsPublished() const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Public Action")
	bool ValidateDefinition(
		const ULLMNPCActionVocabulary* Vocabulary,
		FString& OutError
	) const;

	static FString BuildContentHash(const ULLMNPCPublicActionDefinition& Definition);

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
