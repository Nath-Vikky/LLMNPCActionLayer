#include "Providers/LLMNPCMockProvider.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
struct FMockCandidate
{
	FString DefaultTargetRef;
	float RecommendedAmplitude = 1.0f;
};

struct FMockContext
{
	bool bHasCandidateList = false;
	TMap<FName, FMockCandidate> Candidates;
};

FMockContext ParseMockContext(const FLLMNPCModelTurnRequest& Request)
{
	FMockContext Context;
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Request.ContextJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return Context;
	}

	const TArray<TSharedPtr<FJsonValue>>* Candidates = nullptr;
	if (!Root->TryGetArrayField(TEXT("candidate_templates"), Candidates) || !Candidates)
	{
		return Context;
	}
	Context.bHasCandidateList = true;
	for (const TSharedPtr<FJsonValue>& Value : *Candidates)
	{
		const TSharedPtr<FJsonObject> CandidateObject = Value->AsObject();
		FString SelectionId;
		if (!CandidateObject.IsValid() || !CandidateObject->TryGetStringField(TEXT("template_id"), SelectionId))
		{
			continue;
		}
		FMockCandidate Candidate;
		CandidateObject->TryGetStringField(TEXT("default_target_ref"), Candidate.DefaultTargetRef);
		double RecommendedAmplitude = 1.0;
		if (CandidateObject->TryGetNumberField(TEXT("recommended_amplitude"), RecommendedAmplitude))
		{
			Candidate.RecommendedAmplitude = static_cast<float>(RecommendedAmplitude);
		}
		Context.Candidates.Add(FName(*SelectionId), Candidate);
	}
	return Context;
}

bool IsCandidateAvailable(const FMockContext& Context, FName SelectionId)
{
	return !Context.bHasCandidateList || Context.Candidates.Contains(SelectionId);
}

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

FString BuildMockResponse(const FLLMNPCModelTurnRequest& Request)
{
	const FString UserMessage = FindLastUserMessage(Request);
	const bool bFallback = Request.bFallbackRequest;
	const FMockContext Context = ParseMockContext(Request);
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
	const bool bWantsDirection =
		Normalized.Contains(TEXT("where")) ||
		Normalized.Contains(TEXT("point")) ||
		Normalized.Contains(TEXT("\u5728\u54ea")) ||
		Normalized.Contains(TEXT("\u54ea\u91cc")) ||
		Normalized.Contains(TEXT("\u95e8"));

	FName SelectedAction = NAME_None;
	FName Style(TEXT("neutral"));
	FName ReasonTag(TEXT("no_body_action_needed"));
	FString TargetRef;
	float Amplitude = 1.0f;
	if (bWantsDirection && IsCandidateAvailable(Context, TEXT("gesture.point.target")))
	{
		SelectedAction = TEXT("gesture.point.target");
		ReasonTag = TEXT("contextual_direction");
	}
	else if (bWantsNod && IsCandidateAvailable(Context, TEXT("gesture.nod")))
	{
		SelectedAction = TEXT("gesture.nod");
		ReasonTag = TEXT("contextual_agreement");
	}
	else if (bWantsWave && IsCandidateAvailable(Context, TEXT("gesture.wave.right")))
	{
		SelectedAction = TEXT("gesture.wave.right");
		Style = TEXT("friendly");
		ReasonTag = TEXT("contextual_greeting");
	}
	else if (
		(bWantsWave || bWantsNod) &&
		IsCandidateAvailable(Context, TEXT("gesture.nod"))
	)
	{
		SelectedAction = TEXT("gesture.nod");
		ReasonTag = TEXT("contextual_safe_fallback");
	}

	if (const FMockCandidate* Candidate = Context.Candidates.Find(SelectedAction))
	{
		TargetRef = Candidate->DefaultTargetRef;
		Amplitude = Candidate->RecommendedAmplitude;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), TEXT("llmnpc.model_turn.v1"));
	TSharedRef<FJsonObject> Action = MakeShared<FJsonObject>();
	if (!SelectedAction.IsNone())
	{
		Root->SetStringField(
			TEXT("assistant_text"),
			bFallback ? TEXT("The service is offline, so I used a safe local gesture.") : TEXT("Understood.")
		);
		Action->SetStringField(TEXT("decision"), TEXT("execute_template"));
		Action->SetStringField(TEXT("template_id"), SelectedAction.ToString());
		Action->SetStringField(TEXT("style"), Style.ToString());
		Action->SetStringField(TEXT("reason_tag"), ReasonTag.ToString());
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

	Action->SetStringField(TEXT("target_ref"), TargetRef);
	Action->SetNumberField(TEXT("amplitude"), Amplitude);
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
	Result.ResponseJson = BuildMockResponse(Request);

	if (Callback)
	{
		Callback(Result);
	}
}

void FLLMNPCMockProvider::CancelRequest(const FGuid& RequestId)
{
	static_cast<void>(RequestId);
}
