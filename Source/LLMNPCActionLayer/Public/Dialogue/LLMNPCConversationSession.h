#pragma once

#include "CoreMinimal.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Templates/LLMNPCTemplateCandidate.h"
#include "UObject/Object.h"
#include "LLMNPCConversationSession.generated.h"

UCLASS(BlueprintType)
class LLMNPCACTIONLAYER_API ULLMNPCConversationSession : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="LLM NPC|Conversation")
	void InitializeSession(FName InNPCId, int32 InMaxHistoryMessages = 12);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Conversation")
	FLLMNPCConversationMessage AddMessage(ELLMNPCDialogueRole Role, const FString& Content);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Conversation")
	void AddRecentAction(FName TemplateId);

	UFUNCTION(BlueprintCallable, Category="LLM NPC|Conversation")
	void ResetSession();

	UFUNCTION(BlueprintPure, Category="LLM NPC|Conversation")
	FString BuildRequestContextJson(
		const FGuid& RequestId,
		const TArray<FLLMNPCTemplateCandidate>& Candidates
	) const;

	UFUNCTION(BlueprintPure, Category="LLM NPC|Conversation")
	const TArray<FLLMNPCConversationMessage>& GetMessages() const { return Messages; }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Conversation")
	FGuid GetSessionId() const { return SessionId; }

	UFUNCTION(BlueprintPure, Category="LLM NPC|Conversation")
	FName GetNPCId() const { return NPCId; }

private:
	UPROPERTY(Transient)
	FGuid SessionId;

	UPROPERTY(Transient)
	FName NPCId = NAME_None;

	UPROPERTY(Transient)
	TArray<FLLMNPCConversationMessage> Messages;

	UPROPERTY(Transient)
	TArray<FName> RecentTemplateIds;

	int32 MaxHistoryMessages = 12;
	int32 MaxRecentActions = 8;

	void TrimHistory();
};
