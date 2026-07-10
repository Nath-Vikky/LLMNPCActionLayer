#include "Providers/LLMNPCMockProvider.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString FindLastUserMessage(const FLLMNPCModelTurnRequest& Request)
{
	if (!Request.UserMessage.TrimStartAndEnd().IsEmpty())
	{
		return Request.UserMessage.TrimStartAndEnd();
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Request.ContextJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return FString();
	}

	const TArray<TSharedPtr<FJsonValue>>* Conversation = nullptr;
	if (!Root->TryGetArrayField(TEXT("conversation"), Conversation) || !Conversation)
	{
		return FString();
	}

	for (int32 Index = Conversation->Num() - 1; Index >= 0; --Index)
	{
		const TSharedPtr<FJsonObject> Message = (*Conversation)[Index]->AsObject();
		FString Role;
		FString Content;
		if (
			Message.IsValid() &&
			Message->TryGetStringField(TEXT("role"), Role) &&
			Role == TEXT("user") &&
			Message->TryGetStringField(TEXT("content"), Content)
		)
		{
			return Content.TrimStartAndEnd();
		}
	}

	return FString();
}

FString BuildMockResponse(const FString& UserMessage, bool bFallback)
{
	const FString Normalized = UserMessage.ToLower();
	const bool bSaysHi =
		Normalized == TEXT("hi") ||
		Normalized.StartsWith(TEXT("hi ")) ||
		Normalized.EndsWith(TEXT(" hi")) ||
		Normalized.Contains(TEXT(" hi "));
	const bool bWantsNod =
		Normalized.Contains(TEXT("nod")) ||
		Normalized.Contains(TEXT("agree")) ||
		Normalized.Contains(TEXT("\u70b9\u5934")) ||
		Normalized.Contains(TEXT("\u540c\u610f"));
	const bool bWantsWave =
		Normalized.Contains(TEXT("wave")) ||
		Normalized.Contains(TEXT("hello")) ||
		bSaysHi ||
		Normalized.Contains(TEXT("\u6325\u624b")) ||
		Normalized.Contains(TEXT("\u4f60\u597d"));

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), TEXT("llmnpc.model_turn.v1"));
	TSharedRef<FJsonObject> Action = MakeShared<FJsonObject>();
	if (bWantsNod)
	{
		Root->SetStringField(
			TEXT("assistant_text"),
			bFallback ? TEXT("The service is offline, so I used the local nod command.") : TEXT("Understood. I will nod.")
		);
		Action->SetStringField(TEXT("decision"), TEXT("execute_template"));
		Action->SetStringField(TEXT("template_id"), TEXT("gesture.nod"));
		Action->SetStringField(TEXT("style"), TEXT("neutral"));
		Action->SetStringField(TEXT("reason_tag"), TEXT("explicit_nod_command"));
	}
	else if (bWantsWave)
	{
		Root->SetStringField(
			TEXT("assistant_text"),
			bFallback ? TEXT("The service is offline, so I used the local wave command.") : TEXT("Hello. I will wave.")
		);
		Action->SetStringField(TEXT("decision"), TEXT("execute_template"));
		Action->SetStringField(TEXT("template_id"), TEXT("gesture.wave.right"));
		Action->SetStringField(TEXT("style"), TEXT("friendly"));
		Action->SetStringField(TEXT("reason_tag"), TEXT("explicit_wave_command"));
	}
	else
	{
		Root->SetStringField(
			TEXT("assistant_text"),
			bFallback ? TEXT("The service is offline. Please try again shortly.") : TEXT("I heard you.")
		);
		Action->SetStringField(TEXT("decision"), TEXT("none"));
		Action->SetStringField(TEXT("template_id"), TEXT(""));
		Action->SetStringField(TEXT("style"), TEXT("neutral"));
		Action->SetStringField(TEXT("reason_tag"), TEXT("no_body_action_needed"));
	}

	Action->SetStringField(TEXT("target_ref"), TEXT(""));
	Action->SetNumberField(TEXT("amplitude"), 1.0);
	Action->SetNumberField(TEXT("speed_scale"), 1.0);
	Action->SetNumberField(TEXT("duration_scale"), 1.0);
	Root->SetObjectField(TEXT("action"), Action);

	TSharedRef<FJsonObject> Locomotion = MakeShared<FJsonObject>();
	Locomotion->SetStringField(TEXT("decision"), TEXT("none"));
	Locomotion->SetStringField(TEXT("target_ref"), TEXT(""));
	Locomotion->SetNumberField(TEXT("acceptance_radius_cm"), 0.0);
	Root->SetObjectField(TEXT("locomotion"), Locomotion);

	FString JsonString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(Root, Writer);
	return JsonString;
}
}

void FLLMNPCMockProvider::SendTurn(
	const FLLMNPCModelTurnRequest& Request,
	FLLMNPCModelTurnCallback Callback
)
{
	FLLMNPCModelTurnResult Result;
	Result.RequestId = Request.RequestId;
	Result.bSuccess = true;
	Result.ProviderId = GetProviderId();
	Result.AttemptCount = 1;
	Result.ResponseJson = BuildMockResponse(
		FindLastUserMessage(Request),
		Request.bFallbackRequest
	);

	if (Callback)
	{
		Callback(Result);
	}
}

void FLLMNPCMockProvider::CancelRequest(const FGuid& RequestId)
{
	static_cast<void>(RequestId);
}
