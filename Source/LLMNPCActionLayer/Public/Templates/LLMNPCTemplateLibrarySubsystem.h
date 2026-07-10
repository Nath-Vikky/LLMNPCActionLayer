#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Templates/LLMNPCTemplateCandidate.h"
#include "LLMNPCTemplateLibrarySubsystem.generated.h"

class ULLMNPCMotionTemplate;
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
	const ULLMNPCMotionTemplate* FindPublishedVariant(
		FName PublicActionId,
		FName SkeletonProfileId
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
	int32 GetPublishedTemplateCount() const { return TemplateIndex.Num(); }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Template Library")
	const TArray<FString>& GetScanErrors() const { return ScanErrors; }

private:
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<ULLMNPCMotionTemplate>> TemplateIndex;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<ULLMNPCSkeletonProfile>> SkeletonProfileIndex;

	TMap<FName, TArray<FName>> PublicActionIndex;

	UPROPERTY(Transient)
	TArray<FString> ScanErrors;
};
