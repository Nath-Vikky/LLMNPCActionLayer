#include "Providers/LLMNPCDeepSeekProvider.h"

#include "Containers/Ticker.h"
#include "Dialogue/LLMNPCModelTurnContract.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformTime.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "LLMNPCSettings.h"
#include "Providers/LLMNPCProviderCredentials.h"
#include "Providers/LLMNPCProviderSession.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

struct FLLMNPCDeepSeekProvider::FPendingRequest
{
	FLLMNPCModelTurnRequest Request;
	FLLMNPCModelTurnCallback Callback;
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> HttpRequest;
	int32 Attempt = 0;
	double StartedAtSeconds = 0.0;
};

namespace
{
FLLMNPCModelTurnResult MakeDirectDisabledResult(const FGuid& RequestId)
{
	FLLMNPCModelTurnResult Result;
	Result.RequestId = RequestId;
	Result.ProviderId = TEXT("deepseek_direct_editor");
	Result.ErrorCode = TEXT("LLMNPC_DEEPSEEK_DIRECT_DISABLED");
	return Result;
}
}

FLLMNPCDeepSeekProvider::~FLLMNPCDeepSeekProvider()
{
	TArray<FGuid> RequestIds;
	PendingRequests.GetKeys(RequestIds);
	for (const FGuid& RequestId : RequestIds)
	{
		CancelRequest(RequestId);
	}
}

void FLLMNPCDeepSeekProvider::SendTurn(
	const FLLMNPCModelTurnRequest& Request,
	FLLMNPCModelTurnCallback Callback
)
{
#if !WITH_EDITOR
	if (Callback)
	{
		Callback(MakeDirectDisabledResult(Request.RequestId));
	}
	return;
#else
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings || !Settings->bAllowDirectProviderCallInEditorOnly)
	{
		if (Callback)
		{
			Callback(MakeDirectDisabledResult(Request.RequestId));
		}
		return;
	}

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
	Pending->StartedAtSeconds = FPlatformTime::Seconds();
	PendingRequests.Add(Request.RequestId, Pending);
	StartHttpRequest(Request.RequestId);
#endif
}

void FLLMNPCDeepSeekProvider::CancelRequest(const FGuid& RequestId)
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

void FLLMNPCDeepSeekProvider::StartHttpRequest(const FGuid& RequestId)
{
#if WITH_EDITOR
	TSharedPtr<FPendingRequest>* Found = PendingRequests.Find(RequestId);
	if (!Found || !Found->IsValid())
	{
		return;
	}

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings)
	{
		Complete(RequestId, MakeDirectDisabledResult(RequestId));
		return;
	}

	FString ApiKey;
	ELLMNPCCredentialSource CredentialSource = ELLMNPCCredentialSource::Missing;
	if (!FLLMNPCProviderCredentials::ResolveDeepSeekApiKey(*Settings, ApiKey, CredentialSource))
	{
		FLLMNPCModelTurnResult Result = MakeDirectDisabledResult(RequestId);
		Result.ErrorCode = TEXT("LLMNPC_DEEPSEEK_API_KEY_MISSING");
		Complete(RequestId, MoveTemp(Result));
		return;
	}
	static_cast<void>(CredentialSource);

	TSharedPtr<FPendingRequest> Pending = *Found;
	++Pending->Attempt;
	Pending->HttpRequest = FHttpModule::Get().CreateRequest();
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
	Pending->HttpRequest->SetURL(BaseUrl + TEXT("/chat/completions"));
	Pending->HttpRequest->SetVerb(TEXT("POST"));
	Pending->HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Pending->HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	Pending->HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	Pending->HttpRequest->SetTimeout(Settings->RequestTimeoutSeconds);

	TSharedRef<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("model"), Model);
	Body->SetBoolField(TEXT("stream"), false);
	Body->SetNumberField(TEXT("temperature"), Settings->DeepSeekTemperature);
	Body->SetNumberField(TEXT("max_tokens"), Settings->DeepSeekMaxTokens);
	TSharedRef<FJsonObject> Thinking = MakeShared<FJsonObject>();
	Thinking->SetStringField(TEXT("type"), TEXT("disabled"));
	Body->SetObjectField(TEXT("thinking"), Thinking);
	TSharedRef<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
	ResponseFormat->SetStringField(TEXT("type"), TEXT("json_object"));
	Body->SetObjectField(TEXT("response_format"), ResponseFormat);

	TArray<TSharedPtr<FJsonValue>> Messages;
	TSharedRef<FJsonObject> SystemMessage = MakeShared<FJsonObject>();
	FString SystemPrompt = Settings->DeepSeekSystemPrompt.TrimStartAndEnd();
	if (!SystemPrompt.IsEmpty())
	{
		SystemPrompt += TEXT("\n\n");
	}
	SystemPrompt += FLLMNPCModelTurnContract::GetSelectionSafetyInstruction();
	SystemPrompt += TEXT("\n\n");
	SystemPrompt += FLLMNPCModelTurnContract::GetResponseInstruction();
	SystemMessage->SetStringField(TEXT("role"), TEXT("system"));
	SystemMessage->SetStringField(TEXT("content"), SystemPrompt);
	Messages.Add(MakeShared<FJsonValueObject>(SystemMessage));
	TSharedRef<FJsonObject> UserMessage = MakeShared<FJsonObject>();
	UserMessage->SetStringField(TEXT("role"), TEXT("user"));
	UserMessage->SetStringField(TEXT("content"), Pending->Request.ContextJson);
	Messages.Add(MakeShared<FJsonValueObject>(UserMessage));
	Body->SetArrayField(TEXT("messages"), Messages);

	FString BodyString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyString);
	FJsonSerializer::Serialize(Body, Writer);
	Pending->HttpRequest->SetContentAsString(BodyString);

	const TWeakPtr<FLLMNPCDeepSeekProvider> WeakSelf = AsShared();
	Pending->HttpRequest->OnProcessRequestComplete().BindLambda(
		[WeakSelf, RequestId](
			TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> Request,
			TSharedPtr<IHttpResponse, ESPMode::ThreadSafe> Response,
			bool bWasSuccessful
		)
		{
			if (const TSharedPtr<FLLMNPCDeepSeekProvider> Self = WeakSelf.Pin())
			{
				Self->HandleHttpResponse(RequestId, Request, Response, bWasSuccessful);
			}
		}
	);

	if (!Pending->HttpRequest->ProcessRequest())
	{
		HandleHttpResponse(RequestId, Pending->HttpRequest, nullptr, false);
	}
#endif
}

void FLLMNPCDeepSeekProvider::HandleHttpResponse(
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
		const TWeakPtr<FLLMNPCDeepSeekProvider> WeakSelf = AsShared();
		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda(
				[WeakSelf, RequestId](float)
				{
					if (const TSharedPtr<FLLMNPCDeepSeekProvider> Self = WeakSelf.Pin())
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
	Result.TotalLatencySeconds = static_cast<float>(
		FMath::Max(FPlatformTime::Seconds() - Pending->StartedAtSeconds, 0.0)
	);
	if (!bWasSuccessful || !Response.IsValid())
	{
		Result.ErrorCode = TEXT("LLMNPC_DEEPSEEK_NETWORK_ERROR");
		Complete(RequestId, MoveTemp(Result));
		return;
	}
	if (HttpStatus < 200 || HttpStatus >= 300)
	{
		Result.ErrorCode = FName(*FString::Printf(TEXT("LLMNPC_DEEPSEEK_HTTP_%d"), HttpStatus));
		Complete(RequestId, MoveTemp(Result));
		return;
	}

	if (!ExtractDecisionJson(
		Response->GetContentAsString(),
		Result.ResponseJson,
		Result.ErrorMessage,
		&Result.ProviderModelId,
		&Result.PromptTokens,
		&Result.CompletionTokens,
		&Result.TotalTokens
	))
	{
		Result.ErrorCode = TEXT("LLMNPC_DEEPSEEK_RESPONSE_INVALID");
		Complete(RequestId, MoveTemp(Result));
		return;
	}

	Result.bSuccess = true;
	Complete(RequestId, MoveTemp(Result));
}

void FLLMNPCDeepSeekProvider::Complete(
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

bool FLLMNPCDeepSeekProvider::ExtractDecisionJson(
	const FString& ResponseBody,
	FString& OutDecisionJson,
	FString& OutError,
	FString* OutModelId,
	int32* OutPromptTokens,
	int32* OutCompletionTokens,
	int32* OutTotalTokens
)
{
	OutDecisionJson.Reset();
	OutError.Reset();
	if (OutModelId)
	{
		OutModelId->Reset();
	}
	if (OutPromptTokens)
	{
		*OutPromptTokens = INDEX_NONE;
	}
	if (OutCompletionTokens)
	{
		*OutCompletionTokens = INDEX_NONE;
	}
	if (OutTotalTokens)
	{
		*OutTotalTokens = INDEX_NONE;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_DEEPSEEK_JSON_INVALID");
		return false;
	}

	if (OutModelId)
	{
		Root->TryGetStringField(TEXT("model"), *OutModelId);
	}
	const TSharedPtr<FJsonObject>* Usage = nullptr;
	if (Root->TryGetObjectField(TEXT("usage"), Usage) && Usage && Usage->IsValid())
	{
		auto ReadTokenCount = [Usage](const TCHAR* Field, int32* Output)
		{
			if (!Output)
			{
				return;
			}
			double Value = 0.0;
			if ((*Usage)->TryGetNumberField(Field, Value) && Value >= 0.0)
			{
				*Output = FMath::RoundToInt(Value);
			}
		};
		ReadTokenCount(TEXT("prompt_tokens"), OutPromptTokens);
		ReadTokenCount(TEXT("completion_tokens"), OutCompletionTokens);
		ReadTokenCount(TEXT("total_tokens"), OutTotalTokens);
	}

	const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
	if (!Root->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->IsEmpty())
	{
		OutError = TEXT("LLMNPC_DEEPSEEK_CHOICES_EMPTY");
		return false;
	}

	const TSharedPtr<FJsonObject> Choice = (*Choices)[0]->AsObject();
	if (!Choice.IsValid())
	{
		OutError = TEXT("LLMNPC_DEEPSEEK_CHOICE_INVALID");
		return false;
	}

	FString FinishReason;
	Choice->TryGetStringField(TEXT("finish_reason"), FinishReason);
	if (FinishReason == TEXT("length") || FinishReason == TEXT("content_filter"))
	{
		OutError = FString::Printf(TEXT("LLMNPC_DEEPSEEK_FINISH_%s"), *FinishReason.ToUpper());
		return false;
	}

	const TSharedPtr<FJsonObject>* Message = nullptr;
	if (!Choice->TryGetObjectField(TEXT("message"), Message) || !Message || !Message->IsValid())
	{
		OutError = TEXT("LLMNPC_DEEPSEEK_MESSAGE_MISSING");
		return false;
	}

	if (!(*Message)->TryGetStringField(TEXT("content"), OutDecisionJson))
	{
		OutError = TEXT("LLMNPC_DEEPSEEK_CONTENT_MISSING");
		return false;
	}
	OutDecisionJson = OutDecisionJson.TrimStartAndEnd();
	if (OutDecisionJson.IsEmpty())
	{
		OutError = TEXT("LLMNPC_DEEPSEEK_CONTENT_EMPTY");
		return false;
	}
	return true;
}
