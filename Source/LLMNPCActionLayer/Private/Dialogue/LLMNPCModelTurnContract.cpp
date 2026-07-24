#include "Dialogue/LLMNPCModelTurnContract.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

const FString& FLLMNPCModelTurnContract::GetResponseInstruction()
{
	static const FString Instruction =
		TEXT("The llmnpc.model_turn.v1 response contract is strict. Return exactly one JSON object with no markdown ")
		TEXT("and no unknown fields. Every field below is required, including fields belonging to a decision of none. ")
		TEXT("Root fields: schema_version, assistant_text, action, locomotion. assistant_text must be non-empty. ")
		TEXT("Action fields: decision, template_id, target_ref, amplitude, speed_scale, duration_scale, style, reason_tag. ")
		TEXT("Action decision must be none or execute_template. For none, use an empty template_id and target_ref, ")
		TEXT("numeric modifiers of 1.0, style neutral, and a concise reason_tag. ")
		TEXT("Locomotion fields: decision, target_ref, acceptance_radius_cm. Locomotion decision must be none or move_to. ")
		TEXT("For none, use an empty target_ref and acceptance_radius_cm of 0.0.");
	return Instruction;
}

FString FLLMNPCModelTurnContract::BuildCanonicalNoActionResponse(
	const FString& AssistantText,
	FName ReasonTag
)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), TEXT("llmnpc.model_turn.v1"));
	Root->SetStringField(TEXT("assistant_text"), AssistantText);

	TSharedRef<FJsonObject> Action = MakeShared<FJsonObject>();
	Action->SetStringField(TEXT("decision"), TEXT("none"));
	Action->SetStringField(TEXT("template_id"), TEXT(""));
	Action->SetStringField(TEXT("target_ref"), TEXT(""));
	Action->SetNumberField(TEXT("amplitude"), 1.0);
	Action->SetNumberField(TEXT("speed_scale"), 1.0);
	Action->SetNumberField(TEXT("duration_scale"), 1.0);
	Action->SetStringField(TEXT("style"), TEXT("neutral"));
	Action->SetStringField(TEXT("reason_tag"), ReasonTag.ToString());
	Root->SetObjectField(TEXT("action"), Action);

	TSharedRef<FJsonObject> Locomotion = MakeShared<FJsonObject>();
	Locomotion->SetStringField(TEXT("decision"), TEXT("none"));
	Locomotion->SetStringField(TEXT("target_ref"), TEXT(""));
	Locomotion->SetNumberField(TEXT("acceptance_radius_cm"), 0.0);
	Root->SetObjectField(TEXT("locomotion"), Locomotion);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	return Json;
}
