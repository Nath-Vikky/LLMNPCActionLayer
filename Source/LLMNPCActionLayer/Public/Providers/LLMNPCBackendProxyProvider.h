#pragma once

#include "CoreMinimal.h"
#include "Providers/LLMNPCModelProvider.h"

class IHttpRequest;
class IHttpResponse;

class LLMNPCACTIONLAYER_API FLLMNPCBackendProxyProvider final
	: public ILLMNPCModelProvider
	, public TSharedFromThis<FLLMNPCBackendProxyProvider>
{
public:
	virtual ~FLLMNPCBackendProxyProvider() override;

	virtual void SendTurn(
		const FLLMNPCModelTurnRequest& Request,
		FLLMNPCModelTurnCallback Callback
	) override;

	virtual void CancelRequest(const FGuid& RequestId) override;
	virtual FName GetProviderId() const override { return TEXT("backend_proxy"); }

	static bool ExtractDecisionJson(
		const FString& ResponseBody,
		FString& OutDecisionJson,
		FString& OutError
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
