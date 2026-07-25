#pragma once

#include "CoreMinimal.h"
#include "Providers/LLMNPCModelProvider.h"

class IHttpRequest;
class IHttpResponse;

class LLMNPCACTIONLAYER_API FLLMNPCDeepSeekProvider final
	: public ILLMNPCModelProvider
	, public TSharedFromThis<FLLMNPCDeepSeekProvider>
{
public:
	virtual ~FLLMNPCDeepSeekProvider() override;

	virtual void SendTurn(
		const FLLMNPCModelTurnRequest& Request,
		FLLMNPCModelTurnCallback Callback
	) override;

	virtual void CancelRequest(const FGuid& RequestId) override;
	virtual FName GetProviderId() const override { return TEXT("deepseek_direct_editor"); }
	virtual FLLMNPCProviderCapabilityProfile GetCapabilityProfile() const override
	{
		return FLLMNPCProviderCapabilityProfile::V3();
	}

	static bool ExtractDecisionJson(
		const FString& ResponseBody,
		FString& OutDecisionJson,
		FString& OutError,
		FString* OutModelId = nullptr,
		int32* OutPromptTokens = nullptr,
		int32* OutCompletionTokens = nullptr,
		int32* OutTotalTokens = nullptr
	);

private:
	struct FPendingRequest;
	TMap<FGuid, TSharedPtr<FPendingRequest>> PendingRequests;

	void StartHttpRequest(const FGuid& RequestId);
	void HandleHttpResponse(
		const FGuid& RequestId,
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bWasSuccessful
	);
	void Complete(const FGuid& RequestId, FLLMNPCModelTurnResult Result);
};
