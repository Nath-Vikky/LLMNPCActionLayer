#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Templates/LLMNPCTemplateCatalogTypes.h"
#include "LLMNPCActionVocabulary.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCActionVocabularyEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	FName Tag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	FText EnglishDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	FText ChineseDisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	TArray<FName> Synonyms;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	TArray<ELLMNPCActionVocabularyField> AllowedFields;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	bool bDeprecated = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	FName ReplacementTag = NAME_None;
};

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCActionVocabulary : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	FName VocabularyId = TEXT("llmnpc.default");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	FString SchemaVersion = LLMNPCCatalog::VocabularySchemaVersion;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	FString SemanticVersion = TEXT("1.0.0");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary", meta=(ClampMin="1"))
	int32 Revision = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	TArray<FLLMNPCActionVocabularyEntry> Entries;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="LLM NPC|Vocabulary")
	FString ContentHash;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Vocabulary")
	bool ValidateVocabulary(FString& OutError) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Vocabulary")
	bool IsTagAllowed(
		FName Tag,
		ELLMNPCActionVocabularyField Field,
		bool bAllowDeprecated = false
	) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Vocabulary")
	FName ResolveTag(FName TagOrSynonym) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Vocabulary")
	bool ValidateTags(
		const TArray<FName>& Tags,
		ELLMNPCActionVocabularyField Field,
		FString& OutError
	) const;

	static FString BuildContentHash(const ULLMNPCActionVocabulary& Vocabulary);

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
