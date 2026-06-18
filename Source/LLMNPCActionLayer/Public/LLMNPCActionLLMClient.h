#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LLMNPCActionTypes.h"
#include "LLMNPCActionLLMClient.generated.h"

class IHttpRequest;
class IHttpResponse;

DECLARE_DELEGATE_TwoParams(FOnLLMNPCActionPlanReceived, bool /*bSuccess*/, const FLLMNPCActionPlan& /*Plan*/);

UCLASS()
class LLMNPCACTIONLAYER_API ULLMNPCActionLLMClient : public UObject
{
	GENERATED_BODY()

public:
	void RequestActionPlan(const FString& ContextJson, FOnLLMNPCActionPlanReceived Callback);

private:
	FOnLLMNPCActionPlanReceived PendingCallback;

	void HandleResponse(
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bWasSuccessful
	);
};
