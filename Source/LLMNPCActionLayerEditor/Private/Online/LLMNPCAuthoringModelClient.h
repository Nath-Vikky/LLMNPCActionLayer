#pragma once

#include "CoreMinimal.h"

class IHttpRequest;
class IHttpResponse;

struct FLLMNPCAuthoringJsonRequest
{
	FGuid RequestId;
	FString SystemPrompt;
	FString UserJson;
	float Temperature = 0.1f;
	int32 MaxTokens = 1800;
};

struct FLLMNPCAuthoringJsonResult
{
	bool bSuccess = false;
	FGuid RequestId;
	FName ProviderId = TEXT("deepseek_direct_editor_authoring");
	FString ProviderModelId;
	FString ResponseJson;
	FName ErrorCode = NAME_None;
	int32 HttpStatus = 0;
	int32 AttemptCount = 0;
	float TotalLatencySeconds = 0.0f;
	int32 PromptTokens = INDEX_NONE;
	int32 CompletionTokens = INDEX_NONE;
	int32 TotalTokens = INDEX_NONE;
};

using FLLMNPCAuthoringJsonCallback =
	TFunction<void(const FLLMNPCAuthoringJsonResult&)>;

class FLLMNPCAuthoringModelClient final
	: public TSharedFromThis<FLLMNPCAuthoringModelClient>
{
public:
	~FLLMNPCAuthoringModelClient();

	void Send(
		const FLLMNPCAuthoringJsonRequest& Request,
		FLLMNPCAuthoringJsonCallback Callback
	);

	void Cancel(const FGuid& RequestId);
	bool IsPending(const FGuid& RequestId) const;

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
	void Complete(
		const FGuid& RequestId,
		const FLLMNPCAuthoringJsonResult& Result
	);
};
