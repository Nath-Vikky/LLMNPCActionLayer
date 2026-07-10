#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LLMNPCSelectionAnalyticsSubsystem.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCSelectionAnalyticsEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	double TimestampSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	FName NPCId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	FName ProviderId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	FString PromptVersion;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	TArray<FName> OfferedSelectionIds;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	int32 ExcludedCandidateCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	FName SelectedActionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	FName ResolvedTemplateId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	FName Outcome = TEXT("pending");

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	FName ErrorCode = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	bool bUsedFallback = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection Analytics")
	int64 ContextHash = 0;
};

UCLASS()
class LLMNPCACTIONLAYER_API ULLMNPCSelectionAnalyticsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	void BeginSelection(
		const FGuid& RequestId,
		FName NPCId,
		FName ProviderId,
		const FString& PromptVersion,
		const TArray<FName>& OfferedSelectionIds,
		int32 ExcludedCandidateCount,
		const FString& ContextJson
	);

	void CompleteSelection(
		const FGuid& RequestId,
		FName SelectedActionId,
		FName ResolvedTemplateId,
		FName Outcome,
		FName ErrorCode,
		bool bUsedFallback
	);

	UFUNCTION(BlueprintPure, Category="LLM NPC|Selection Analytics")
	const TArray<FLLMNPCSelectionAnalyticsEvent>& GetRecentEvents() const { return RecentEvents; }

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Selection Analytics")
	void ClearEvents();

private:
	UPROPERTY(Transient)
	TArray<FLLMNPCSelectionAnalyticsEvent> RecentEvents;
};
