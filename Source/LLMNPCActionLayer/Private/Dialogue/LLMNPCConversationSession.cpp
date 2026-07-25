#include "Dialogue/LLMNPCConversationSession.h"

#include "Protocol/LLMNPCProtocolCompatibility.h"
#include "Protocol/LLMNPCTurnRequestV3Adapter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const TCHAR* RoleToString(ELLMNPCDialogueRole Role)
{
	switch (Role)
	{
	case ELLMNPCDialogueRole::Assistant:
		return TEXT("assistant");
	case ELLMNPCDialogueRole::System:
		return TEXT("system");
	case ELLMNPCDialogueRole::Player:
	default:
		return TEXT("user");
	}
}

TArray<TSharedPtr<FJsonValue>> NamesToJson(const TArray<FName>& Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Names.Num());
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> StringsToJson(const TArray<FString>& Strings)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Strings.Num());
	for (const FString& Value : Strings)
	{
		Values.Add(MakeShared<FJsonValueString>(Value));
	}
	return Values;
}

TArray<TSharedPtr<FJsonValue>> RangeToJson(const FVector2D& Range)
{
	return {
		MakeShared<FJsonValueNumber>(Range.X),
		MakeShared<FJsonValueNumber>(Range.Y)
	};
}

FString SanitizePromptText(const FString& Value, int32 MaxLength)
{
	FString Result;
	Result.Reserve(FMath::Min(Value.Len(), MaxLength));
	for (const TCHAR Character : Value)
	{
		if (Result.Len() >= MaxLength)
		{
			break;
		}
		if (Character == TEXT('\n') || Character == TEXT('\t') || !FChar::IsControl(Character))
		{
			Result.AppendChar(Character);
		}
	}
	return Result.TrimStartAndEnd();
}

TSharedRef<FJsonObject> BuildV3CandidateObject(
	const FLLMNPCTemplateCandidate& Candidate
)
{
	TSharedRef<FJsonObject> CandidateObject = MakeShared<FJsonObject>();
	CandidateObject->SetStringField(
		TEXT("selection_id"),
		Candidate.SelectionId.ToString()
	);
	CandidateObject->SetStringField(
		TEXT("selection_summary"),
		SanitizePromptText(Candidate.SelectionSummary, 240)
	);
	TArray<FString> Suitable;
	for (const FString& Value : Candidate.SuitableWhen)
	{
		Suitable.Add(SanitizePromptText(Value, 160));
	}
	TArray<FString> Avoid;
	for (const FString& Value : Candidate.AvoidWhen)
	{
		Avoid.Add(SanitizePromptText(Value, 160));
	}
	CandidateObject->SetArrayField(TEXT("suitable_when"), StringsToJson(Suitable));
	CandidateObject->SetArrayField(TEXT("avoid_when"), StringsToJson(Avoid));
	CandidateObject->SetArrayField(
		TEXT("body_regions"),
		NamesToJson(Candidate.BodyRegionTags)
	);
	CandidateObject->SetArrayField(
		TEXT("semantic_effects"),
		NamesToJson(Candidate.SemanticEffectTags)
	);

	TSharedRef<FJsonObject> TargetContract = MakeShared<FJsonObject>();
	TargetContract->SetBoolField(TEXT("requires_target"), Candidate.bRequiresTarget);
	TargetContract->SetArrayField(
		TEXT("allowed_categories"),
		NamesToJson(Candidate.TargetCategoryTags)
	);
	CandidateObject->SetObjectField(TEXT("target_contract"), TargetContract);

	TArray<TSharedPtr<FJsonValue>> StyleValues;
	for (const FLLMNPCCandidateStyleOption& Style : Candidate.StyleOptions)
	{
		TSharedRef<FJsonObject> StyleObject = MakeShared<FJsonObject>();
		StyleObject->SetStringField(TEXT("style"), Style.Style.ToString());
		StyleObject->SetArrayField(TEXT("amplitude"), RangeToJson(Style.AmplitudeRange));
		StyleObject->SetArrayField(TEXT("speed_scale"), RangeToJson(Style.SpeedRange));
		StyleObject->SetArrayField(TEXT("duration_scale"), RangeToJson(Style.DurationRange));
		StyleObject->SetBoolField(TEXT("mirror_allowed"), Style.bMirrorAllowed);
		StyleValues.Add(MakeShared<FJsonValueObject>(StyleObject));
	}
	CandidateObject->SetArrayField(TEXT("style_options"), StyleValues);
	CandidateObject->SetStringField(
		TEXT("recommended_style"),
		Candidate.RecommendedStyle.ToString()
	);
	CandidateObject->SetArrayField(
		TEXT("allowed_target_refs"),
		StringsToJson(Candidate.AllowedTargetRefs)
	);
	CandidateObject->SetStringField(
		TEXT("default_target_ref"),
		Candidate.DefaultTargetRef
	);
	CandidateObject->SetBoolField(
		TEXT("mirror_recommended"),
		Candidate.bMirrorRecommended
	);
	return CandidateObject;
}

TSharedRef<FJsonObject> BuildV2CandidateObject(
	const FLLMNPCTemplateCandidate& Candidate
)
{
	TSharedRef<FJsonObject> CandidateObject = MakeShared<FJsonObject>();
	CandidateObject->SetStringField(
		TEXT("template_id"),
		Candidate.SelectionId.ToString()
	);
	CandidateObject->SetStringField(
		TEXT("description"),
		SanitizePromptText(Candidate.Description.ToString(), 240)
	);
	CandidateObject->SetArrayField(TEXT("intent_tags"), NamesToJson(Candidate.IntentTags));
	CandidateObject->SetArrayField(TEXT("emotion_tags"), NamesToJson(Candidate.EmotionTags));
	CandidateObject->SetBoolField(TEXT("requires_target"), Candidate.bRequiresTarget);
	CandidateObject->SetArrayField(
		TEXT("allowed_target_refs"),
		StringsToJson(Candidate.AllowedTargetRefs)
	);
	CandidateObject->SetStringField(TEXT("default_target_ref"), Candidate.DefaultTargetRef);
	CandidateObject->SetNumberField(
		TEXT("recommended_amplitude"),
		Candidate.RecommendedAmplitude
	);
	CandidateObject->SetNumberField(
		TEXT("recommended_speed_scale"),
		Candidate.RecommendedSpeedScale
	);
	CandidateObject->SetNumberField(
		TEXT("recommended_duration_scale"),
		Candidate.RecommendedDurationScale
	);
	CandidateObject->SetStringField(
		TEXT("recommended_style"),
		Candidate.RecommendedStyle.ToString()
	);
	CandidateObject->SetBoolField(
		TEXT("mirror_recommended"),
		Candidate.bMirrorRecommended
	);
	TSharedRef<FJsonObject> Modifiers = MakeShared<FJsonObject>();
	Modifiers->SetArrayField(TEXT("amplitude"), RangeToJson(Candidate.AmplitudeRange));
	Modifiers->SetArrayField(TEXT("speed_scale"), RangeToJson(Candidate.SpeedRange));
	Modifiers->SetArrayField(TEXT("duration_scale"), RangeToJson(Candidate.DurationRange));
	Modifiers->SetArrayField(TEXT("styles"), NamesToJson(Candidate.AllowedStyles));
	CandidateObject->SetObjectField(TEXT("allowed_modifiers"), Modifiers);
	return CandidateObject;
}
}

void ULLMNPCConversationSession::InitializeSession(FName InNPCId, int32 InMaxHistoryMessages)
{
	NPCId = InNPCId.IsNone() ? FName(TEXT("npc")) : InNPCId;
	MaxHistoryMessages = FMath::Clamp(InMaxHistoryMessages, 2, 64);
	ResetSession();
}

FLLMNPCConversationMessage ULLMNPCConversationSession::AddMessage(
	ELLMNPCDialogueRole Role,
	const FString& Content
)
{
	FLLMNPCConversationMessage Message;
	Message.MessageId = FGuid::NewGuid();
	Message.Role = Role;
	Message.Content = Content.TrimStartAndEnd();
	if (!Message.Content.IsEmpty())
	{
		Messages.Add(Message);
		TrimHistory();
	}
	return Message;
}

void ULLMNPCConversationSession::AddRecentAction(FName TemplateId)
{
	AddActionHistory(TemplateId, TemplateId, FString(), NAME_None);
}

void ULLMNPCConversationSession::AddActionHistory(
	FName SelectionId,
	FName ResolvedTemplateId,
	const FString& TargetRef,
	FName ReasonTag
)
{
	if (SelectionId.IsNone())
	{
		return;
	}

	FLLMNPCActionHistoryEntry& Entry = ActionHistory.AddDefaulted_GetRef();
	Entry.SelectionId = SelectionId;
	Entry.ResolvedTemplateId = ResolvedTemplateId;
	Entry.TargetRef = TargetRef.TrimStartAndEnd();
	Entry.ReasonTag = ReasonTag;
	Entry.TimestampSeconds = FPlatformTime::Seconds();
	if (ActionHistory.Num() > MaxRecentActions)
	{
		ActionHistory.RemoveAt(0, ActionHistory.Num() - MaxRecentActions);
	}
}

void ULLMNPCConversationSession::ResetSession()
{
	SessionId = FGuid::NewGuid();
	Messages.Reset();
	ActionHistory.Reset();
}

FString ULLMNPCConversationSession::BuildRequestContextJson(
	const FGuid& RequestId,
	const TArray<FLLMNPCTemplateCandidate>& Candidates
) const
{
	return BuildContextualRequestJson(
		RequestId,
		Candidates,
		FLLMNPCSelectionContextSnapshot(),
		TEXT("llmnpc.selection_prompt.v1")
	);
}

FString ULLMNPCConversationSession::BuildContextualRequestJson(
	const FGuid& RequestId,
	const TArray<FLLMNPCTemplateCandidate>& Candidates,
	const FLLMNPCSelectionContextSnapshot& Context,
	const FString& PromptVersion
) const
{
	return BuildContextualRequestJsonForSchema(
		RequestId,
		Candidates,
		Context,
		PromptVersion,
		FLLMNPCProtocolCompatibility::CurrentTurnRequestSchema()
	);
}

FString ULLMNPCConversationSession::BuildContextualRequestJsonForSchema(
	const FGuid& RequestId,
	const TArray<FLLMNPCTemplateCandidate>& Candidates,
	const FLLMNPCSelectionContextSnapshot& Context,
	const FString& PromptVersion,
	const FString& RequestSchemaVersion
) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		RequestSchemaVersion
	);
	Root->SetStringField(TEXT("prompt_version"), PromptVersion);
	Root->SetStringField(TEXT("request_id"), RequestId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(TEXT("session_id"), SessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
	Root->SetStringField(TEXT("npc_id"), NPCId.ToString());

	TArray<TSharedPtr<FJsonValue>> Conversation;
	Conversation.Reserve(Messages.Num());
	for (const FLLMNPCConversationMessage& Message : Messages)
	{
		TSharedRef<FJsonObject> MessageObject = MakeShared<FJsonObject>();
		MessageObject->SetStringField(TEXT("role"), RoleToString(Message.Role));
		MessageObject->SetStringField(TEXT("content"), Message.Content);
		Conversation.Add(MakeShared<FJsonValueObject>(MessageObject));
	}
	Root->SetArrayField(TEXT("conversation"), Conversation);

	TSharedRef<FJsonObject> ContextObject = MakeShared<FJsonObject>();
	TSharedRef<FJsonObject> EmotionObject = MakeShared<FJsonObject>();
	EmotionObject->SetStringField(TEXT("primary"), Context.Emotion.PrimaryEmotion.ToString());
	EmotionObject->SetNumberField(TEXT("intensity"), Context.Emotion.Intensity);
	EmotionObject->SetNumberField(TEXT("valence"), Context.Emotion.Valence);
	EmotionObject->SetNumberField(TEXT("arousal"), Context.Emotion.Arousal);
	ContextObject->SetObjectField(TEXT("emotion"), EmotionObject);

	TSharedRef<FJsonObject> PersonalityObject = MakeShared<FJsonObject>();
	PersonalityObject->SetStringField(TEXT("profile_id"), Context.Personality.ProfileId.ToString());
	PersonalityObject->SetNumberField(TEXT("expressiveness"), Context.Personality.Expressiveness);
	PersonalityObject->SetNumberField(TEXT("shyness"), Context.Personality.Shyness);
	PersonalityObject->SetNumberField(TEXT("sociability"), Context.Personality.Sociability);
	PersonalityObject->SetArrayField(TEXT("tags"), NamesToJson(Context.Personality.PersonalityTags));
	ContextObject->SetObjectField(TEXT("personality"), PersonalityObject);

	TSharedRef<FJsonObject> RelationshipObject = MakeShared<FJsonObject>();
	RelationshipObject->SetStringField(TEXT("other_actor_ref"), Context.Relationship.OtherActorRef);
	RelationshipObject->SetNumberField(TEXT("familiarity"), Context.Relationship.Familiarity);
	RelationshipObject->SetNumberField(TEXT("trust"), Context.Relationship.Trust);
	RelationshipObject->SetNumberField(TEXT("affinity"), Context.Relationship.Affinity);
	RelationshipObject->SetArrayField(TEXT("tags"), NamesToJson(Context.Relationship.RelationshipTags));
	ContextObject->SetObjectField(TEXT("relationship"), RelationshipObject);
	ContextObject->SetArrayField(TEXT("active_states"), NamesToJson(Context.ActiveStates));

	TArray<TSharedPtr<FJsonValue>> TargetValues;
	for (const FLLMNPCSceneTargetContext& Target : Context.AvailableTargets)
	{
		TSharedRef<FJsonObject> TargetObject = MakeShared<FJsonObject>();
		TargetObject->SetStringField(TEXT("target_ref"), Target.TargetRef);
		TargetObject->SetStringField(TEXT("category"), Target.Category.ToString());
		TargetObject->SetArrayField(TEXT("semantic_tags"), NamesToJson(Target.SemanticTags));
		TargetObject->SetNumberField(TEXT("salience"), Target.Salience);
		TargetValues.Add(MakeShared<FJsonValueObject>(TargetObject));
	}
	ContextObject->SetArrayField(TEXT("scene_targets"), TargetValues);
	Root->SetObjectField(TEXT("selection_context"), ContextObject);

	TArray<TSharedPtr<FJsonValue>> CandidateValues;
	CandidateValues.Reserve(Candidates.Num());
	for (const FLLMNPCTemplateCandidate& Candidate : Candidates)
	{
		const TSharedRef<FJsonObject> CandidateObject =
			RequestSchemaVersion == TEXT("llmnpc.turn_request.v3")
			? BuildV3CandidateObject(Candidate)
			: BuildV2CandidateObject(Candidate);
		CandidateValues.Add(MakeShared<FJsonValueObject>(CandidateObject));
	}
	Root->SetArrayField(TEXT("candidate_templates"), CandidateValues);

	const double NowSeconds = FPlatformTime::Seconds();
	TArray<TSharedPtr<FJsonValue>> HistoryValues;
	HistoryValues.Reserve(ActionHistory.Num());
	for (const FLLMNPCActionHistoryEntry& Entry : ActionHistory)
	{
		TSharedRef<FJsonObject> HistoryObject = MakeShared<FJsonObject>();
		HistoryObject->SetStringField(TEXT("selection_id"), Entry.SelectionId.ToString());
		HistoryObject->SetStringField(TEXT("target_ref"), Entry.TargetRef);
		HistoryObject->SetStringField(TEXT("reason_tag"), Entry.ReasonTag.ToString());
		HistoryObject->SetNumberField(
			TEXT("age_seconds"),
			FMath::Max(0.0, NowSeconds - Entry.TimestampSeconds)
		);
		HistoryValues.Add(MakeShared<FJsonValueObject>(HistoryObject));
	}
	Root->SetArrayField(TEXT("recent_action_history"), HistoryValues);

	FString JsonString;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(Root, Writer);
	return JsonString;
}

void ULLMNPCConversationSession::TrimHistory()
{
	if (Messages.Num() > MaxHistoryMessages)
	{
		Messages.RemoveAt(0, Messages.Num() - MaxHistoryMessages);
	}
}
