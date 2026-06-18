#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LLMNPCMotionTypes.h"
#include "LLMNPCAPIClient.generated.h"

class IHttpRequest;
class IHttpResponse;

DECLARE_DELEGATE_TwoParams(FOnLLMMotionPlanReceived, bool /*bSuccess*/, const FLLMMotionPlan& /*Plan*/);

UCLASS()
class LLMNPCACTIONLAYER_API ULLMNPCAPIClient : public UObject
{
	GENERATED_BODY()

public:
	void RequestMotionPlan(const FString& ContextJson, FOnLLMMotionPlanReceived Callback);

private:
	FOnLLMMotionPlanReceived PendingCallback;

	void HandleResponse(
		TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
		TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
		bool bWasSuccessful
	);
};
