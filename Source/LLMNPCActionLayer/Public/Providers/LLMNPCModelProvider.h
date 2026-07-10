#pragma once

#include "CoreMinimal.h"
#include "LLMNPCModelProvider.generated.h"

USTRUCT(BlueprintType)
struct FLLMNPCModelTurnRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model")
	FGuid RequestId;

	UPROPERTY(BlueprintReadWrite, Category="LLM NPC|Model")
	FString ContextJson;
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
};
