#include "LLMNPCAPIClient.h"

#include "LLMNPCActionLayer.h"
#include "LLMNPCSettings.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"

void ULLMNPCAPIClient::RequestMotionPlan(const FString& ContextJson, FOnLLMMotionPlanReceived Callback)
{
	PendingCallback = Callback;

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings || Settings->ProviderEndpoint.TrimStartAndEnd().IsEmpty())
	{
		FLLMMotionPlan EmptyPlan;
		PendingCallback.ExecuteIfBound(false, EmptyPlan);
		return;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Settings->ProviderEndpoint);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Request->SetTimeout(Settings->RequestTimeoutSeconds);

#if WITH_EDITOR
	if (Settings->bAllowDirectProviderCallInEditorOnly)
	{
		const FString ApiKey = FPlatformMisc::GetEnvironmentVariable(*Settings->ApiKeyEnvironmentVariable);
		if (!ApiKey.IsEmpty())
		{
			Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
		}
	}
#endif

	Request->SetContentAsString(ContextJson);
	Request->OnProcessRequestComplete().BindUObject(this, &ULLMNPCAPIClient::HandleResponse);

	if (!Request->ProcessRequest())
	{
		FLLMMotionPlan EmptyPlan;
		PendingCallback.ExecuteIfBound(false, EmptyPlan);
	}
}

void ULLMNPCAPIClient::HandleResponse(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	bool bWasSuccessful
)
{
	FLLMMotionPlan Plan;

	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPCMotion API request failed before a valid response was received."));
		PendingCallback.ExecuteIfBound(false, Plan);
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code < 200 || Code >= 300)
	{
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPCMotion API failed: HTTP %d"), Code);
		PendingCallback.ExecuteIfBound(false, Plan);
		return;
	}

	const FString Body = Response->GetContentAsString();
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Body, &Plan, 0, 0))
	{
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPCMotion API parse failed."));
		PendingCallback.ExecuteIfBound(false, Plan);
		return;
	}

	PendingCallback.ExecuteIfBound(true, Plan);
}
