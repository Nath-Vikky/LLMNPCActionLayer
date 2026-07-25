#pragma once

#include "CoreMinimal.h"
#include "Templates/LLMNPCTemplateCandidate.h"
#include "Templates/LLMNPCTemplateCatalogTypes.h"

class ULLMNPCActionVocabulary;
class ULLMNPCMotionTemplate;
class ULLMNPCPublicActionDefinition;

class LLMNPCACTIONLAYER_API FLLMNPCTemplateSearchIndex
{
public:
	bool Build(
		const TArray<ULLMNPCMotionTemplate*>& Templates,
		const TArray<ULLMNPCPublicActionDefinition*>& Definitions,
		const ULLMNPCActionVocabulary* Vocabulary,
		const TSet<FName>& AvailableSkeletonProfiles
	);

	void Reset();

	const ULLMNPCMotionTemplate* FindTemplate(FName TemplateId) const;
	const ULLMNPCPublicActionDefinition* FindDefinition(FName PublicActionId) const;
	const TArray<FName>* FindVariants(FName PublicActionId) const;

	void GetTemplateIds(TArray<FName>& OutTemplateIds) const;
	void GetPublicActionIds(TArray<FName>& OutPublicActionIds) const;
	void QueryDescriptionTokens(const FString& Query, TArray<FName>& OutPublicActionIds) const;
	bool BuildRuntimeCandidate(
		FName PublicActionId,
		FName SkeletonProfileId,
		FLLMNPCTemplateCandidate& OutCandidate
	) const;

	const TArray<FLLMNPCCatalogDiagnostic>& GetDiagnostics() const { return Diagnostics; }
	const FString& GetCatalogHash() const { return CatalogHash; }
	int32 GetTemplateCount() const { return TemplateIndex.Num(); }
	int32 GetPublicActionCount() const { return DefinitionIndex.Num(); }

private:
	TMap<FName, const ULLMNPCMotionTemplate*> TemplateIndex;
	TMap<FName, const ULLMNPCPublicActionDefinition*> DefinitionIndex;
	TMap<FName, TArray<FName>> PublicActionIndex;
	TMap<FString, TArray<FName>> DescriptionTokenIndex;
	TArray<FLLMNPCCatalogDiagnostic> Diagnostics;
	FString CatalogHash;
};
