#include "Dialogue/LLMNPCModelTurnValidator.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

namespace
{
const FString ModelTurnSchemaVersion(TEXT("llmnpc.model_turn.v1"));
const FName DecisionNone(TEXT("none"));
const FName DecisionExecuteTemplate(TEXT("execute_template"));

bool ValidateFields(
	const TSharedPtr<FJsonObject>& Object,
	const TSet<FString>& AllowedFields,
	const TCHAR* ErrorPrefix,
	FString& OutError
)
{
	if (!Object.IsValid())
	{
		OutError = FString::Printf(TEXT("%s_OBJECT_MISSING"), ErrorPrefix);
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!AllowedFields.Contains(Pair.Key))
		{
			OutError = FString::Printf(TEXT("%s_FIELD_UNKNOWN:%s"), ErrorPrefix, *Pair.Key);
			return false;
		}
	}

	return true;
}

bool GetRequiredString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& OutValue,
	FString& OutError
)
{
	if (!Object->TryGetStringField(Field, OutValue))
	{
		OutError = FString::Printf(TEXT("LLMNPC_MODEL_FIELD_MISSING:%s"), Field);
		return false;
	}

	OutValue = OutValue.TrimStartAndEnd();
	return true;
}

bool GetOptionalFiniteNumber(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	float DefaultValue,
	float& OutValue,
	FString& OutError
)
{
	OutValue = DefaultValue;
	if (!Object->HasField(Field))
	{
		return true;
	}

	double Number = 0.0;
	if (!Object->TryGetNumberField(Field, Number) || !FMath::IsFinite(Number))
	{
		OutError = FString::Printf(TEXT("LLMNPC_MODEL_NUMBER_INVALID:%s"), Field);
		return false;
	}

	OutValue = static_cast<float>(Number);
	return FMath::IsFinite(OutValue);
}
}

bool FLLMNPCModelTurnParser::Parse(
	const FString& JsonString,
	FLLMNPCModelTurnDecision& OutDecision,
	FString& OutError
)
{
	OutDecision = FLLMNPCModelTurnDecision();
	OutError.Reset();

	if (JsonString.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LLMNPC_MODEL_RESPONSE_EMPTY");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_MODEL_JSON_INVALID");
		return false;
	}

	static const TSet<FString> RootFields = {
		TEXT("schema_version"),
		TEXT("assistant_text"),
		TEXT("action"),
		TEXT("locomotion")
	};
	if (!ValidateFields(Root, RootFields, TEXT("LLMNPC_MODEL_ROOT"), OutError))
	{
		return false;
	}

	if (
		!GetRequiredString(Root, TEXT("schema_version"), OutDecision.SchemaVersion, OutError) ||
		OutDecision.SchemaVersion != ModelTurnSchemaVersion
	)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("LLMNPC_MODEL_SCHEMA_UNSUPPORTED");
		}
		return false;
	}

	if (!GetRequiredString(Root, TEXT("assistant_text"), OutDecision.AssistantText, OutError))
	{
		return false;
	}
	if (OutDecision.AssistantText.IsEmpty())
	{
		OutError = TEXT("LLMNPC_MODEL_ASSISTANT_TEXT_EMPTY");
		return false;
	}
	if (OutDecision.AssistantText.Len() > 4000)
	{
		OutError = TEXT("LLMNPC_MODEL_ASSISTANT_TEXT_TOO_LONG");
		return false;
	}

	const TSharedPtr<FJsonObject>* ActionObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("action"), ActionObject) || !ActionObject || !ActionObject->IsValid())
	{
		OutError = TEXT("LLMNPC_MODEL_ACTION_MISSING");
		return false;
	}

	static const TSet<FString> ActionFields = {
		TEXT("decision"),
		TEXT("template_id"),
		TEXT("target_ref"),
		TEXT("amplitude"),
		TEXT("speed_scale"),
		TEXT("duration_scale"),
		TEXT("style"),
		TEXT("reason_tag")
	};
	if (!ValidateFields(*ActionObject, ActionFields, TEXT("LLMNPC_MODEL_ACTION"), OutError))
	{
		return false;
	}

	FString StringValue;
	if (!GetRequiredString(*ActionObject, TEXT("decision"), StringValue, OutError))
	{
		return false;
	}
	OutDecision.Action.Decision = FName(*StringValue);

	if (!GetRequiredString(*ActionObject, TEXT("template_id"), StringValue, OutError))
	{
		return false;
	}
	if (!StringValue.IsEmpty())
	{
		if (StringValue.Contains(TEXT("/")) || StringValue.Contains(TEXT("\\")))
		{
			OutError = TEXT("LLMNPC_MODEL_TEMPLATE_ID_PATH_FORBIDDEN");
			return false;
		}
		if (StringValue.Len() > 128)
		{
			OutError = TEXT("LLMNPC_MODEL_TEMPLATE_ID_TOO_LONG");
			return false;
		}
		OutDecision.Action.TemplateId = FName(*StringValue);
	}

	if (
		!GetRequiredString(*ActionObject, TEXT("target_ref"), OutDecision.Action.TargetRef, OutError) ||
		!GetRequiredString(*ActionObject, TEXT("style"), StringValue, OutError)
	)
	{
		return false;
	}
	if (OutDecision.Action.TargetRef.Len() > 128 || StringValue.Len() > 64)
	{
		OutError = TEXT("LLMNPC_MODEL_ACTION_STRING_TOO_LONG");
		return false;
	}
	OutDecision.Action.Style = StringValue.IsEmpty() ? FName(TEXT("neutral")) : FName(*StringValue);
	if (!GetRequiredString(*ActionObject, TEXT("reason_tag"), StringValue, OutError))
	{
		return false;
	}
	if (StringValue.Len() > 64)
	{
		OutError = TEXT("LLMNPC_MODEL_REASON_TAG_TOO_LONG");
		return false;
	}
	OutDecision.Action.ReasonTag = FName(*StringValue);

	if (
		!(*ActionObject)->HasField(TEXT("amplitude")) ||
		!(*ActionObject)->HasField(TEXT("speed_scale")) ||
		!(*ActionObject)->HasField(TEXT("duration_scale"))
	)
	{
		OutError = TEXT("LLMNPC_MODEL_ACTION_MODIFIER_MISSING");
		return false;
	}
	if (
		!GetOptionalFiniteNumber(*ActionObject, TEXT("amplitude"), 1.0f, OutDecision.Action.Amplitude, OutError) ||
		!GetOptionalFiniteNumber(*ActionObject, TEXT("speed_scale"), 1.0f, OutDecision.Action.SpeedScale, OutError) ||
		!GetOptionalFiniteNumber(*ActionObject, TEXT("duration_scale"), 1.0f, OutDecision.Action.DurationScale, OutError)
	)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* LocomotionObject = nullptr;
	if (!Root->TryGetObjectField(TEXT("locomotion"), LocomotionObject) || !LocomotionObject || !LocomotionObject->IsValid())
	{
		OutError = TEXT("LLMNPC_MODEL_LOCOMOTION_MISSING");
		return false;
	}
	static const TSet<FString> LocomotionFields = {
			TEXT("decision"),
			TEXT("target_ref"),
			TEXT("acceptance_radius_cm")
		};
		if (!ValidateFields(*LocomotionObject, LocomotionFields, TEXT("LLMNPC_MODEL_LOCOMOTION"), OutError))
		{
			return false;
		}

		if (!GetRequiredString(*LocomotionObject, TEXT("decision"), StringValue, OutError))
		{
			return false;
		}
		OutDecision.Locomotion.Decision = FName(*StringValue);
		if (!GetRequiredString(*LocomotionObject, TEXT("target_ref"), OutDecision.Locomotion.TargetRef, OutError))
		{
			return false;
		}
		if (OutDecision.Locomotion.TargetRef.Len() > 128)
		{
			OutError = TEXT("LLMNPC_MODEL_LOCOMOTION_TARGET_TOO_LONG");
			return false;
		}
		if (!(*LocomotionObject)->HasField(TEXT("acceptance_radius_cm")))
		{
			OutError = TEXT("LLMNPC_MODEL_LOCOMOTION_RADIUS_MISSING");
			return false;
		}
	if (!GetOptionalFiniteNumber(
			*LocomotionObject,
			TEXT("acceptance_radius_cm"),
			0.0f,
			OutDecision.Locomotion.AcceptanceRadiusCm,
			OutError
		))
	{
		return false;
	}
	if (OutDecision.Locomotion.AcceptanceRadiusCm < 0.0f)
	{
		OutError = TEXT("LLMNPC_MODEL_LOCOMOTION_RADIUS_NEGATIVE");
		return false;
	}

	return true;
}

bool FLLMNPCModelTurnValidator::ValidateAndResolve(
	FLLMNPCModelTurnDecision& InOutDecision,
	const ULLMNPCTemplateLibrarySubsystem& TemplateLibrary,
	FName SkeletonProfileId,
	const ULLMNPCMotionTemplate*& OutTemplate,
	FLLMNPCTemplateModifiers& OutModifiers,
	FString& OutError
)
{
	OutTemplate = nullptr;
	OutModifiers = FLLMNPCTemplateModifiers();
	OutError.Reset();

	if (InOutDecision.Locomotion.Decision != DecisionNone)
	{
		OutError = TEXT("LLMNPC_MODEL_LOCOMOTION_UNSUPPORTED");
		return false;
	}

	if (InOutDecision.Action.Decision == DecisionNone)
	{
		if (!InOutDecision.Action.TemplateId.IsNone())
		{
			OutError = TEXT("LLMNPC_MODEL_ACTION_NONE_HAS_TEMPLATE");
			return false;
		}
		return true;
	}

	if (InOutDecision.Action.Decision != DecisionExecuteTemplate)
	{
		OutError = TEXT("LLMNPC_MODEL_ACTION_DECISION_UNSUPPORTED");
		return false;
	}

	if (InOutDecision.Action.TemplateId.IsNone())
	{
		OutError = TEXT("LLMNPC_MODEL_TEMPLATE_ID_MISSING");
		return false;
	}

	OutTemplate = TemplateLibrary.ResolveRuntimeModelTemplate(
		InOutDecision.Action.TemplateId,
		SkeletonProfileId
	);
	if (!OutTemplate)
	{
		OutError = TEXT("LLMNPC_MODEL_TEMPLATE_NOT_RUNTIME_SELECTABLE");
		return false;
	}

	if (OutTemplate->Metadata.bRequiresTarget && InOutDecision.Action.TargetRef.IsEmpty())
	{
		OutError = TEXT("LLMNPC_MODEL_TEMPLATE_TARGET_REQUIRED");
		return false;
	}

	if (
		!OutTemplate->ModifierPolicy.AllowedStyleTags.IsEmpty() &&
		!OutTemplate->ModifierPolicy.AllowedStyleTags.Contains(InOutDecision.Action.Style)
	)
	{
		OutError = TEXT("LLMNPC_MODEL_TEMPLATE_STYLE_FORBIDDEN");
		return false;
	}

	InOutDecision.Action.Amplitude = FMath::Clamp(
		InOutDecision.Action.Amplitude,
		OutTemplate->ModifierPolicy.AmplitudeRange.X,
		OutTemplate->ModifierPolicy.AmplitudeRange.Y
	);
	InOutDecision.Action.SpeedScale = FMath::Clamp(
		InOutDecision.Action.SpeedScale,
		OutTemplate->ModifierPolicy.SpeedRange.X,
		OutTemplate->ModifierPolicy.SpeedRange.Y
	);
	InOutDecision.Action.DurationScale = FMath::Clamp(
		InOutDecision.Action.DurationScale,
		OutTemplate->ModifierPolicy.DurationRange.X,
		OutTemplate->ModifierPolicy.DurationRange.Y
	);

	OutModifiers.TargetRef = InOutDecision.Action.TargetRef;
	OutModifiers.Amplitude = InOutDecision.Action.Amplitude;
	OutModifiers.SpeedScale = InOutDecision.Action.SpeedScale;
	OutModifiers.DurationScale = InOutDecision.Action.DurationScale;
	OutModifiers.Style = InOutDecision.Action.Style;
	return true;
}
