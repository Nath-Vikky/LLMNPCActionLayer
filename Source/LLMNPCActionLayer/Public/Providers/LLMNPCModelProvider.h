#pragma once

#include "CoreMinimal.h"
#include "Protocol/LLMNPCProviderCapabilityTypes.h"
#include "LLMNPCModelProvider.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCModelTurnRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model")
	FGuid RequestId;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model")
	FGuid SessionId;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model")
	FName NPCId = NAME_None;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model")
	FString UserMessage;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model")
	FString ContextJson;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model")
	bool bFallbackRequest = false;
};

USTRUCT(BlueprintType)
struct FLLMNPCModelTurnResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	FGuid RequestId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	FString ResponseJson;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	FName ErrorCode = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	FName ProviderId = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	FString ProviderModelId;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	int32 HttpStatus = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	int32 AttemptCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	float TotalLatencySeconds = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	int32 PromptTokens = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	int32 CompletionTokens = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category="LLM NPC|Model")
	int32 TotalTokens = INDEX_NONE;
};

using FLLMNPCModelTurnCallback = TFunction<void(const FLLMNPCModelTurnResult&)>;

class LLMNPCACTIONLAYER_API ILLMNPCModelProvider
{
public:
	virtual ~ILLMNPCModelProvider() = default;

	virtual void SendTurn(
		const FLLMNPCModelTurnRequest& Request,
		FLLMNPCModelTurnCallback Callback
	) = 0;

	virtual void CancelRequest(const FGuid& RequestId) = 0;

	virtual FName GetProviderId() const = 0;

	virtual FLLMNPCProviderCapabilityProfile GetCapabilityProfile() const
	{
		return FLLMNPCProviderCapabilityProfile();
	}
};
