#include "Online/LLMNPCAuthoringModelClient.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "LLMNPCSettings.h"
#include "Providers/LLMNPCDeepSeekProvider.h"
#include "Providers/LLMNPCProviderCredentials.h"
#include "Providers/LLMNPCProviderSession.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

struct FLLMNPCAuthoringModelClient::FPendingRequest
{
	FLLMNPCAuthoringJsonRequest Request;
	FLLMNPCAuthoringJsonCallback Callback;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
	int32 Attempt = 0;
	double StartedAtSeconds = 0.0;
	FTSTicker::FDelegateHandle WatchdogHandle;
};

namespace
{
FLLMNPCAuthoringJsonResult MakeFailure(
	const FGuid& RequestId,
	FName ErrorCode
)
{
	FLLMNPCAuthoringJsonResult Result;
	Result.RequestId = RequestId;
	Result.ErrorCode = ErrorCode;
	return Result;
}
}

FLLMNPCAuthoringModelClient::~FLLMNPCAuthoringModelClient()
{
	TArray<FGuid> RequestIds;
	PendingRequests.GetKeys(RequestIds);
	for (const FGuid& RequestId : RequestIds)
	{
		Cancel(RequestId);
	}
}

void FLLMNPCAuthoringModelClient::Send(
	const FLLMNPCAuthoringJsonRequest& Request,
	FLLMNPCAuthoringJsonCallback Callback
)
{
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings || !Settings->bAllowDirectProviderCallInEditorOnly)
	{
		if (Callback)
		{
			Callback(MakeFailure(
				Request.RequestId,
				TEXT("LLMNPC_AUTHORING_DIRECT_PROVIDER_DISABLED")
			));
		}
		return;
	}
	if (
		!Request.RequestId.IsValid() ||
		PendingRequests.Contains(Request.RequestId) ||
		Request.SystemPrompt.TrimStartAndEnd().IsEmpty() ||
		Request.UserJson.TrimStartAndEnd().IsEmpty()
	)
	{
		if (Callback)
		{
			Callback(MakeFailure(
				Request.RequestId,
				TEXT("LLMNPC_AUTHORING_REQUEST_INVALID")
			));
		}
		return;
	}

	TSharedPtr<FPendingRequest> Pending = MakeShared<FPendingRequest>();
	Pending->Request = Request;
	Pending->Callback = MoveTemp(Callback);
	Pending->StartedAtSeconds = FPlatformTime::Seconds();
	PendingRequests.Add(Request.RequestId, Pending);
	const float WatchdogSeconds = FMath::Clamp(
		Request.TimeoutSeconds > 0.0f
			? Request.TimeoutSeconds
			: Settings->AuthoringSandboxRequestTimeoutSeconds,
		2.0f,
		120.0f
	);
	const TWeakPtr<FLLMNPCAuthoringModelClient> WeakSelf = AsShared();
	Pending->WatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[WeakSelf, RequestId = Request.RequestId](float)
			{
				if (
					const TSharedPtr<FLLMNPCAuthoringModelClient> Self =
						WeakSelf.Pin()
				)
				{
					Self->HandleWatchdogTimeout(RequestId);
				}
				return false;
			}
		),
		WatchdogSeconds
	);
	StartHttpRequest(Request.RequestId);
}

void FLLMNPCAuthoringModelClient::Cancel(const FGuid& RequestId)
{
	TSharedPtr<FPendingRequest> Pending;
	if (!PendingRequests.RemoveAndCopyValue(RequestId, Pending) || !Pending)
	{
		return;
	}
	if (Pending->HttpRequest)
	{
		Pending->HttpRequest->CancelRequest();
	}
	if (Pending->WatchdogHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(
			Pending->WatchdogHandle
		);
		Pending->WatchdogHandle.Reset();
	}
	FLLMNPCAuthoringJsonResult Result = MakeFailure(
		RequestId,
		TEXT("LLMNPC_AUTHORING_REQUEST_CANCELLED")
	);
	Result.AttemptCount = Pending->Attempt;
	Result.TotalLatencySeconds = static_cast<float>(
		FMath::Max(FPlatformTime::Seconds() - Pending->StartedAtSeconds, 0.0)
	);
	if (Pending->Callback)
	{
		Pending->Callback(Result);
	}
}

bool FLLMNPCAuthoringModelClient::IsPending(
	const FGuid& RequestId
) const
{
	return PendingRequests.Contains(RequestId);
}

void FLLMNPCAuthoringModelClient::StartHttpRequest(
	const FGuid& RequestId
)
{
	TSharedPtr<FPendingRequest>* Found = PendingRequests.Find(RequestId);
	if (!Found || !Found->IsValid())
	{
		return;
	}
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings)
	{
		Complete(
			RequestId,
			MakeFailure(RequestId, TEXT("LLMNPC_AUTHORING_SETTINGS_MISSING"))
		);
		return;
	}

	FString ApiKey;
	ELLMNPCCredentialSource CredentialSource =
		ELLMNPCCredentialSource::Missing;
	if (!FLLMNPCProviderCredentials::ResolveDeepSeekApiKey(
		*Settings,
		ApiKey,
		CredentialSource
	))
	{
		Complete(
			RequestId,
			MakeFailure(
				RequestId,
				TEXT("LLMNPC_AUTHORING_API_KEY_MISSING")
			)
		);
		return;
	}
	static_cast<void>(CredentialSource);

	FString BaseUrl = Settings->DeepSeekBaseUrl.TrimStartAndEnd();
	FString Model = Settings->DeepSeekModel.TrimStartAndEnd();
	FLLMNPCProviderSessionOverrides SessionOverrides;
	if (FLLMNPCProviderSession::GetSessionOverrides(
		FLLMNPCProviderCredentials::DeepSeekProviderId(),
		SessionOverrides
	))
	{
		BaseUrl = SessionOverrides.BaseUrl;
		Model = SessionOverrides.Model;
	}
	while (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1);
	}

	TSharedPtr<FPendingRequest> Pending = *Found;
	++Pending->Attempt;
	Pending->HttpRequest = FHttpModule::Get().CreateRequest();
	Pending->HttpRequest->SetURL(BaseUrl + TEXT("/chat/completions"));
	Pending->HttpRequest->SetVerb(TEXT("POST"));
	Pending->HttpRequest->SetHeader(
		TEXT("Content-Type"),
		TEXT("application/json")
	);
	Pending->HttpRequest->SetHeader(
		TEXT("Accept"),
		TEXT("application/json")
	);
	Pending->HttpRequest->SetHeader(
		TEXT("Authorization"),
		FString::Printf(TEXT("Bearer %s"), *ApiKey)
	);
	ApiKey.Reset();
	Pending->HttpRequest->SetTimeout(Settings->RequestTimeoutSeconds);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Model);
	Body->SetBoolField(TEXT("stream"), false);
	Body->SetNumberField(
		TEXT("temperature"),
		FMath::Clamp(Pending->Request.Temperature, 0.0f, 0.4f)
	);
	Body->SetNumberField(
		TEXT("max_tokens"),
		FMath::Clamp(Pending->Request.MaxTokens, 512, 4096)
	);
	TSharedRef<FJsonObject> Thinking = MakeShared<FJsonObject>();
	Thinking->SetStringField(TEXT("type"), TEXT("disabled"));
	Body->SetObjectField(TEXT("thinking"), Thinking);
	TSharedRef<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
	ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
	Body->SetObjectField(TEXT("response_format"), ResponseFormat);

	TArray<TSharedPtr<FJsonValue>> Messages;
	TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
	SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
	SystemMessage->SetStringField(
		TEXT("content"),
		Pending->Request.SystemPrompt
	);
	Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));
	TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	UserMessage->SetStringField(
		TEXT("content"),
		Pending->Request.UserJson
	);
	Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
	Body->SetArrayField(TEXT("messages"), Messages);

	FString BodyString;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(Body, Writer);
	Pending->HttpRequest->SetContentAsString(BodyString);

	const TWeakPtr<FLLMNPCAuthoringModelClient> WeakSelf = AsShared();
	Pending->HttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakSelf, RequestId](
			TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest,
			TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> HttpResponse,
			bool bWasSuccessful
		)
		{
			if (
				const TSharedPtr<FLLMNPCAuthoringModelClient> Self =
					WeakSelf.Pin()
			)
			{
				Self->HandleHttpResponse(
					RequestId,
					HttpRequest,
					HttpResponse,
					bWasSuccessful
				);
			}
		}
	);
	if (!Pending->HttpRequest->ProcessRequest())
	{
		HandleHttpResponse(
			RequestId,
			Pending->HttpRequest,
			nullptr,
			false
		);
	}
}

void FLLMNPCAuthoringModelClient::HandleHttpResponse(
	const FGuid& RequestId,
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
	TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
	bool bWasSuccessful
)
{
	static_cast<void>(Request);
	TSharedPtr<FPendingRequest>* Found = PendingRequests.Find(RequestId);
	if (!Found || !Found->IsValid())
	{
		return;
	}
	TSharedPtr<FPendingRequest> Pending = *Found;
	Pending->HttpRequest.Reset();

	const int32 HttpStatus =
		Response.IsValid() ? Response->GetResponseCode() : 0;
	const bool bRetryable =
		!bWasSuccessful ||
		HttpStatus == 429 ||
		HttpStatus == 500 ||
		HttpStatus == 502 ||
		HttpStatus == 503 ||
		HttpStatus == 504;
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const int32 MaxAttempts =
		Settings ? Settings->MaxProviderRetries + 1 : 1;
	if (bRetryable && Pending->Attempt < MaxAttempts)
	{
		const float BaseDelay =
			Settings ? Settings->ProviderRetryBaseDelaySeconds : 0.35f;
		const float Delay =
			BaseDelay * FMath::Pow(2.0f, Pending->Attempt - 1);
		const TWeakPtr<FLLMNPCAuthoringModelClient> WeakSelf = AsShared();
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[WeakSelf, RequestId](float)
				{
					if (
						const TSharedPtr<FLLMNPCAuthoringModelClient> Self =
							WeakSelf.Pin()
					)
					{
						Self->StartHttpRequest(RequestId);
					}
					return false;
				}
			),
			Delay
		);
		return;
	}

	FLLMNPCAuthoringJsonResult Result;
	Result.RequestId = RequestId;
	Result.HttpStatus = HttpStatus;
	Result.AttemptCount = Pending->Attempt;
	Result.TotalLatencySeconds = static_cast<float>(
		FMath::Max(FPlatformTime::Seconds() - Pending->StartedAtSeconds, 0.0)
	);
	if (!bWasSuccessful || !Response.IsValid())
	{
		Result.ErrorCode = TEXT("LLMNPC_AUTHORING_NETWORK_ERROR");
		Complete(RequestId, Result);
		return;
	}
	if (HttpStatus < 200 || HttpStatus >= 300)
	{
		Result.ErrorCode = FName(*FString::Printf(
			TEXT("LLMNPC_AUTHORING_HTTP_%d"),
			HttpStatus
		));
		Complete(RequestId, Result);
		return;
	}

	FString ExtractionError;
	if (!FLLMNPCDeepSeekProvider::ExtractDecisionJson(
		Response->GetContentAsString(),
		Result.ResponseJson,
		ExtractionError,
		&Result.ProviderModelId,
		&Result.PromptTokens,
		&Result.CompletionTokens,
		&Result.TotalTokens
	))
	{
		Result.ErrorCode = TEXT("LLMNPC_AUTHORING_PROVIDER_RESPONSE_INVALID");
		Complete(RequestId, Result);
		return;
	}
	Result.bSuccess = true;
	Complete(RequestId, Result);
}

void FLLMNPCAuthoringModelClient::HandleWatchdogTimeout(
	const FGuid& RequestId
)
{
	TSharedPtr<FPendingRequest>* Found =
		PendingRequests.Find(RequestId);
	if (!Found || !Found->IsValid())
	{
		return;
	}
	const TSharedPtr<FPendingRequest> Pending = *Found;
	if (Pending->HttpRequest)
	{
		Pending->HttpRequest->CancelRequest();
		Pending->HttpRequest.Reset();
	}
	Pending->WatchdogHandle.Reset();

	FLLMNPCAuthoringJsonResult Result = MakeFailure(
		RequestId,
		TEXT("LLMNPC_AUTHORING_REQUEST_TIMEOUT")
	);
	Result.AttemptCount = Pending->Attempt;
	Result.TotalLatencySeconds = static_cast<float>(
		FMath::Max(
			FPlatformTime::Seconds() - Pending->StartedAtSeconds,
			0.0
		)
	);
	Complete(RequestId, Result);
}

void FLLMNPCAuthoringModelClient::Complete(
	const FGuid& RequestId,
	const FLLMNPCAuthoringJsonResult& Result
)
{
	TSharedPtr<FPendingRequest> Pending;
	if (!PendingRequests.RemoveAndCopyValue(RequestId, Pending) || !Pending)
	{
		return;
	}
	if (Pending->WatchdogHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(
			Pending->WatchdogHandle
		);
		Pending->WatchdogHandle.Reset();
	}
	if (Pending->Callback)
	{
		Pending->Callback(Result);
	}
}
