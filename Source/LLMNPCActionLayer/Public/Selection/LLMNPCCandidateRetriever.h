#pragma once

#include "CoreMinimal.h"
#include "Context/LLMNPCContextTypes.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Templates/LLMNPCTemplateCandidate.h"
#include "UObject/Object.h"
#include "LLMNPCCandidateRetriever.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCCandidateExclusion
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection")
	FName SelectionId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection")
	FName Reason = NAME_None;
};

USTRUCT(BlueprintType)
struct FLLMNPCCandidateRetrievalRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Selection")
	FString UserMessage;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Selection")
	TArray<FLLMNPCTemplateCandidate> SourceCandidates;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Selection")
	FLLMNPCSelectionContextSnapshot Context;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Selection")
	TArray<FLLMNPCActionHistoryEntry> ActionHistory;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Selection")
	double NowSeconds = 0.0;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Selection", meta=(ClampMin="1", ClampMax="32"))
	int32 MaxCandidates = 8;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Selection", meta=(ClampMin="0.0"))
	float RepeatSuppressionSeconds = 2.0f;
};

USTRUCT(BlueprintType)
struct FLLMNPCCandidateRetrievalResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection")
	TArray<FLLMNPCTemplateCandidate> Candidates;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Selection")
	TArray<FLLMNPCCandidateExclusion> Exclusions;
};

UCLASS()
class LLMNPCACTIONLAYER_API ULLMNPCCandidateRetriever : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="LLM NPC|Selection")
	static FLLMNPCCandidateRetrievalResult Retrieve(const FLLMNPCCandidateRetrievalRequest& Request);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Selection")
	static bool ApplySelectionPolicy(
		UPARAM(ref) FLLMNPCModelTurnDecision& Decision,
		const TArray<FLLMNPCTemplateCandidate>& OfferedCandidates,
		FString& OutError
	);
};
