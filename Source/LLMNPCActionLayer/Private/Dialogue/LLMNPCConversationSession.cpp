#include "Dialogue/LLMNPCConversationSession.h"

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
	if (TemplateId.IsNone())
	{
		return;
	}

	RecentTemplateIds.Add(TemplateId);
	if (RecentTemplateIds.Num() > MaxRecentActions)
	{
		RecentTemplateIds.RemoveAt(0, RecentTemplateIds.Num() - MaxRecentActions);
	}
}

void ULLMNPCConversationSession::ResetSession()
{
	SessionId = FGuid::NewGuid();
	Messages.Reset();
	RecentTemplateIds.Reset();
}

FString ULLMNPCConversationSession::BuildRequestContextJson(
	const FGuid& RequestId,
	const TArray<FLLMNPCTemplateCandidate>& Candidates
) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), TEXT("llmnpc.turn_request.v1"));
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

	TArray<TSharedPtr<FJsonValue>> CandidateValues;
	CandidateValues.Reserve(Candidates.Num());
	for (const FLLMNPCTemplateCandidate& Candidate : Candidates)
	{
		TSharedRef<FJsonObject> CandidateObject = MakeShared<FJsonObject>();
		CandidateObject->SetStringField(TEXT("template_id"), Candidate.SelectionId.ToString());
		CandidateObject->SetStringField(TEXT("description"), Candidate.Description.ToString());
		CandidateObject->SetArrayField(TEXT("intent_tags"), NamesToJson(Candidate.IntentTags));
		CandidateObject->SetArrayField(TEXT("emotion_tags"), NamesToJson(Candidate.EmotionTags));
		CandidateObject->SetBoolField(TEXT("requires_target"), Candidate.bRequiresTarget);

		TSharedRef<FJsonObject> Modifiers = MakeShared<FJsonObject>();
		Modifiers->SetArrayField(TEXT("amplitude"), {
			MakeShared<FJsonValueNumber>(Candidate.AmplitudeRange.X),
			MakeShared<FJsonValueNumber>(Candidate.AmplitudeRange.Y)
		});
		Modifiers->SetArrayField(TEXT("speed_scale"), {
			MakeShared<FJsonValueNumber>(Candidate.SpeedRange.X),
			MakeShared<FJsonValueNumber>(Candidate.SpeedRange.Y)
		});
		Modifiers->SetArrayField(TEXT("duration_scale"), {
			MakeShared<FJsonValueNumber>(Candidate.DurationRange.X),
			MakeShared<FJsonValueNumber>(Candidate.DurationRange.Y)
		});
		Modifiers->SetArrayField(TEXT("styles"), NamesToJson(Candidate.AllowedStyles));
		CandidateObject->SetObjectField(TEXT("allowed_modifiers"), Modifiers);
		CandidateValues.Add(MakeShared<FJsonValueObject>(CandidateObject));
	}
	Root->SetArrayField(TEXT("candidate_templates"), CandidateValues);
	Root->SetArrayField(TEXT("recent_action_history"), NamesToJson(RecentTemplateIds));

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
