#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/LLMNPCTemplateCandidate.h"
#include "Templates/LLMNPCTemplateSearchIndex.h"
#include "LLMNPCTemplateLibrarySubsystem.generated.h"

class ULLMNPCActionVocabulary;
class ULLMNPCMotionTemplate;
class ULLMNPCPublicActionDefinition;
class ULLMNPCSkeletonProfile;

UCLASS()
class LLMNPCACTIONLAYER_API ULLMNPCTemplateLibrarySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Template Library")
	void RefreshLibrary();

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	const ULLMNPCMotionTemplate* FindPublishedTemplate(FName TemplateId) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	const ULLMNPCPublicActionDefinition* FindPublishedPublicAction(
		FName PublicActionId
	) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	const ULLMNPCMotionTemplate* FindPublishedVariant(
		FName PublicActionId,
		FName SkeletonProfileId
	) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	const ULLMNPCMotionTemplate* ResolvePublishedVariant(
		FName PublicActionId,
		FName SkeletonProfileId,
		FName StyleTag,
		int32 RandomSeed
	) const;

	const ULLMNPCMotionTemplate* ResolvePublishedVariantWithConstraints(
		FName PublicActionId,
		FName SkeletonProfileId,
		FName StyleTag,
		int32 RandomSeed,
		bool bRequireMirror
	) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	const ULLMNPCSkeletonProfile* FindSkeletonProfile(FName ProfileId) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	const ULLMNPCMotionTemplate* ResolveRuntimeModelTemplate(
		FName SelectionId,
		FName SkeletonProfileId
	) const;

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Template Library")
	void QueryRuntimeCandidates(
		FName SkeletonProfileId,
		TArray<FLLMNPCTemplateCandidate>& OutCandidates
	) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	int32 GetPublishedTemplateCount() const { return CatalogIndex.GetTemplateCount(); }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	int32 GetPublishedPublicActionCount() const { return CatalogIndex.GetPublicActionCount(); }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	FString GetCatalogHash() const { return CatalogIndex.GetCatalogHash(); }

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Template Library")
	void GetPublishedTemplateIdsForProfile(
		FName SkeletonProfileId,
		TArray<FName>& OutTemplateIds
	) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	const TArray<FString>& GetScanErrors() const { return ScanErrors; }

	const TArray<FLLMNPCCatalogDiagnostic>& GetCatalogDiagnostics() const
	{
		return CatalogIndex.GetDiagnostics();
	}

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULLMNPCMotionTemplate>> LoadedTemplates;

	UPROPERTY(Transient)
	TArray<TObjectPtr<ULLMNPCPublicActionDefinition>> LoadedPublicActions;

	UPROPERTY(Transient)
	TObjectPtr<ULLMNPCActionVocabulary> LoadedVocabulary;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<ULLMNPCSkeletonProfile>> SkeletonProfileIndex;

	FLLMNPCTemplateSearchIndex CatalogIndex;

	UPROPERTY(Transient)
	TArray<FString> ScanErrors;
};
