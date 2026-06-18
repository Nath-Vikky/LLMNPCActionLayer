#include "LLMNPCActionLLMClient.h"

#include "LLMNPCActionLayer.h"
#include "LLMNPCActionSettings.h"

#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "JsonObjectConverter.h"

void ULLMNPCActionLLMClient::RequestActionPlan(
	const FString& ContextJson,
	FOnLLMNPCActionPlanReceived Callback
)
{
	PendingCallback = Callback;

	const ULLMNPCActionSettings* Settings = GetDefault<ULLMNPCActionSettings>();
	if (!Settings)
	{
		FLLMNPCActionPlan EmptyPlan;
		PendingCallback.ExecuteIfBound(false, EmptyPlan);
		return;
	}

	if (Settings->ProviderEndpoint.TrimStartAndEnd().IsEmpty())
	{
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPC: Provider endpoint is empty."));
		FLLMNPCActionPlan EmptyPlan;
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
			Request->SetHeader(
				TEXT("Authorization"),
				FString::Printf(TEXT("Bearer %s"), *ApiKey)
			);
		}
	}
#endif

	Request->SetContentAsString(ContextJson);
	Request->OnProcessRequestComplete().BindUObject(
		this,
		&ULLMNPCActionLLMClient::HandleResponse
	);

	if (!Request->ProcessRequest())
	{
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPC: Failed to start LLM HTTP request."));
		FLLMNPCActionPlan EmptyPlan;
		PendingCallback.ExecuteIfBound(false, EmptyPlan);
	}
}

void ULLMNPCActionLLMClient::HandleResponse(
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	bool bWasSuccessful
)
{
	FLLMNPCActionPlan Plan;

	if (!bWasSuccessful || !Response.IsValid())
	{
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPC: LLM request failed before a valid response was received."));
		PendingCallback.ExecuteIfBound(false, Plan);
		return;
	}

	const int32 Code = Response->GetResponseCode();
	if (Code < 200 || Code >= 300)
	{
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPC: LLM request failed with HTTP %d"), Code);
		PendingCallback.ExecuteIfBound(false, Plan);
		return;
	}

	const FString Body = Response->GetContentAsString();
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Body, &Plan, 0, 0))
	{
		UE_LOG(LogLLMNPCActionLayer, Warning, TEXT("LLMNPC: Failed to parse LLM response as an action plan."));
		PendingCallback.ExecuteIfBound(false, Plan);
		return;
	}

	PendingCallback.ExecuteIfBound(true, Plan);
}
