#include "Providers/LLMNPCBackendProxyProvider.h"

#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "LLMNPCSettings.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

struct FLLMNPCBackendProxyProvider::FPendingRequest
{
	FLLMNPCModelTurnRequest Request;
	FLLMNPCModelTurnCallback Callback;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
	int32 Attempt = 0;
};

FLLMNPCBackendProxyProvider::~FLLMNPCBackendProxyProvider()
{
	TArray<FGuid> RequestIds;
	PendingRequests.GetKeys(RequestIds);
	for (const FGuid& RequestId : RequestIds)
	{
		CancelRequest(RequestId);
	}
}

void FLLMNPCBackendProxyProvider::SendTurn(
	const FLLMNPCModelTurnRequest& Request,
	FLLMNPCModelTurnCallback Callback
)
{
	if (!Request.RequestId.IsValid() || PendingRequests.Contains(Request.RequestId))
	{
		FLLMNPCModelTurnResult Result;
		Result.RequestId = Request.RequestId;
		Result.ProviderId = GetProviderId();
		Result.ErrorCode = TEXT("LLMNPC_PROVIDER_REQUEST_ID_INVALID");
		if (Callback)
		{
			Callback(Result);
		}
		return;
	}

	TSharedPtr<FPendingRequest> Pending = MakeShared<FPendingRequest>();
	Pending->Request = Request;
	Pending->Callback = MoveTemp(Callback);
	PendingRequests.Add(Request.RequestId, Pending);
	StartHttpRequest(Request.RequestId);
}

void FLLMNPCBackendProxyProvider::CancelRequest(const FGuid& RequestId)
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

	FLLMNPCModelTurnResult Result;
	Result.RequestId = RequestId;
	Result.ProviderId = GetProviderId();
	Result.ErrorCode = TEXT("LLMNPC_PROVIDER_CANCELLED");
	if (Pending->Callback)
	{
		Pending->Callback(Result);
	}
}

void FLLMNPCBackendProxyProvider::StartHttpRequest(const FGuid& RequestId)
{
	TSharedPtr<FPendingRequest>* Found = PendingRequests.Find(RequestId);
	if (!Found || !Found->IsValid())
	{
		return;
	}

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings || Settings->BackendProxyEndpoint.TrimStartAndEnd().IsEmpty())
	{
		FLLMNPCModelTurnResult Result;
		Result.RequestId = RequestId;
		Result.ProviderId = GetProviderId();
		Result.ErrorCode = TEXT("LLMNPC_BACKEND_ENDPOINT_MISSING");
		Complete(RequestId, MoveTemp(Result));
		return;
	}

	TSharedPtr<FPendingRequest> Pending = *Found;
	++Pending->Attempt;
	Pending->HttpRequest = FHttpModule::Get().CreateRequest();
	Pending->HttpRequest->SetURL(Settings->BackendProxyEndpoint);
	Pending->HttpRequest->SetVerb(TEXT("POST"));
	Pending->HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Pending->HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Pending->HttpRequest->SetHeader(
		TEXT("X-Request-Id"),
		Pending->Request.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)
	);
	Pending->HttpRequest->SetTimeout(Settings->RequestTimeoutSeconds);
	Pending->HttpRequest->SetContentAsString(Pending->Request.ContextJson);

	const TWeakPtr<FLLMNPCBackendProxyProvider> WeakSelf = AsShared();
	Pending->HttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakSelf, RequestId](
			TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
			TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
			bool bWasSuccessful
		)
		{
			if (const TSharedPtr<FLLMNPCBackendProxyProvider> Self = WeakSelf.Pin())
			{
				Self->HandleHttpResponse(RequestId, Request, Response, bWasSuccessful);
			}
		}
	);

	if (!Pending->HttpRequest->ProcessRequest())
	{
		HandleHttpResponse(RequestId, Pending->HttpRequest, nullptr, false);
	}
}

void FLLMNPCBackendProxyProvider::HandleHttpResponse(
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
	const int32 HttpStatus = Response.IsValid() ? Response->GetResponseCode() : 0;
	const bool bRetryable = !bWasSuccessful || HttpStatus == 429 || HttpStatus == 500 || HttpStatus == 503;
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const int32 MaxAttempts = Settings ? Settings->MaxProviderRetries + 1 : 1;
	if (bRetryable && Pending->Attempt < MaxAttempts)
	{
		const float BaseDelay = Settings ? Settings->ProviderRetryBaseDelaySeconds : 0.35f;
		const float Delay = BaseDelay * FMath::Pow(2.0f, Pending->Attempt - 1);
		const TWeakPtr<FLLMNPCBackendProxyProvider> WeakSelf = AsShared();
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[WeakSelf, RequestId](float)
				{
					if (const TSharedPtr<FLLMNPCBackendProxyProvider> Self = WeakSelf.Pin())
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

	FLLMNPCModelTurnResult Result;
	Result.RequestId = RequestId;
	Result.ProviderId = GetProviderId();
	Result.HttpStatus = HttpStatus;
	Result.AttemptCount = Pending->Attempt;
	if (!bWasSuccessful || !Response.IsValid())
	{
		Result.ErrorCode = TEXT("LLMNPC_BACKEND_NETWORK_ERROR");
		Complete(RequestId, MoveTemp(Result));
		return;
	}
	if (HttpStatus < 200 || HttpStatus >= 300)
	{
		Result.ErrorCode = FName(*FString::Printf(TEXT("LLMNPC_BACKEND_HTTP_%d"), HttpStatus));
		Complete(RequestId, MoveTemp(Result));
		return;
	}

	if (!ExtractDecisionJson(Response->GetContentAsString(), Result.ResponseJson, Result.ErrorMessage))
	{
		Result.ErrorCode = TEXT("LLMNPC_BACKEND_RESPONSE_INVALID");
		Complete(RequestId, MoveTemp(Result));
		return;
	}

	Result.bSuccess = true;
	Complete(RequestId, MoveTemp(Result));
}

void FLLMNPCBackendProxyProvider::Complete(
	const FGuid& RequestId,
	FLLMNPCModelTurnResult Result
)
{
	TSharedPtr<FPendingRequest> Pending;
	if (!PendingRequests.RemoveAndCopyValue(RequestId, Pending) || !Pending)
	{
		return;
	}

	if (Pending->Callback)
	{
		Pending->Callback(Result);
	}
}

bool FLLMNPCBackendProxyProvider::ExtractDecisionJson(
	const FString& ResponseBody,
	FString& OutDecisionJson,
	FString& OutError
)
{
	OutDecisionJson.Reset();
	OutError.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_BACKEND_JSON_INVALID");
		return false;
	}

	TSharedPtr<FJsonObject> DecisionObject;
	const TSharedPtr<FJsonObject>* DecisionObjectPtr = nullptr;
	if (Root->TryGetObjectField(TEXT("decision"), DecisionObjectPtr) && DecisionObjectPtr)
	{
		DecisionObject = *DecisionObjectPtr;
	}
	else if (Root->HasField(TEXT("schema_version")))
	{
		DecisionObject = Root;
	}
	else
	{
		OutError = TEXT("LLMNPC_BACKEND_DECISION_MISSING");
		return false;
	}

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutDecisionJson);
	return FJsonSerializer::Serialize(DecisionObject.ToSharedRef(), Writer);
}
