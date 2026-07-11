#include "Authoring/LLMNPCTemplateDraftImporter.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
bool IsSafeIdentifier(const FString& Value, int32 MaxLength)
{
	if (Value.IsEmpty() || Value.Len() > MaxLength || Value.Contains(TEXT("/")) || Value.Contains(TEXT("\\")))
	{
		return false;
	}
	for (const TCHAR Character : Value)
	{
		if (
			!FChar::IsAlnum(Character) &&
			Character != TEXT('.') &&
			Character != TEXT('_') &&
			Character != TEXT('-')
		)
		{
			return false;
		}
	}
	return true;
}

bool IsSemanticVersion(const FString& Value)
{
	TArray<FString> Parts;
	Value.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 3)
	{
		return false;
	}
	for (const FString& Part : Parts)
	{
		if (Part.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Character : Part)
		{
			if (!FChar::IsDigit(Character))
			{
				return false;
			}
		}
	}
	return true;
}

bool ValidateFields(
	const TSharedPtr<FJsonObject>& Object,
	const TSet<FString>& AllowedFields,
	const TCHAR* Prefix,
	FString& OutError
)
{
	if (!Object.IsValid())
	{
		OutError = FString::Printf(TEXT("%s_OBJECT_MISSING"), Prefix);
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!AllowedFields.Contains(Pair.Key))
		{
			OutError = FString::Printf(TEXT("%s_FIELD_UNKNOWN:%s"), Prefix, *Pair.Key);
			return false;
		}
	}
	return true;
}

bool GetString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& OutValue,
	FString& OutError,
	bool bAllowEmpty = false
)
{
	if (!Object.IsValid() || !Object->TryGetStringField(Field, OutValue))
	{
		OutError = FString::Printf(TEXT("LLMNPC_DRAFT_FIELD_MISSING:%s"), Field);
		return false;
	}
	OutValue = OutValue.TrimStartAndEnd();
	if (!bAllowEmpty && OutValue.IsEmpty())
	{
		OutError = FString::Printf(TEXT("LLMNPC_DRAFT_FIELD_EMPTY:%s"), Field);
		return false;
	}
	return true;
}

bool GetBool(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	bool& OutValue,
	FString& OutError
)
{
	if (!Object.IsValid() || !Object->TryGetBoolField(Field, OutValue))
	{
		OutError = FString::Printf(TEXT("LLMNPC_DRAFT_BOOL_INVALID:%s"), Field);
		return false;
	}
	return true;
}

bool GetNumber(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	float& OutValue,
	FString& OutError
)
{
	double Number = 0.0;
	if (
		!Object.IsValid() ||
		!Object->TryGetNumberField(Field, Number) ||
		!FMath::IsFinite(Number)
	)
	{
		OutError = FString::Printf(TEXT("LLMNPC_DRAFT_NUMBER_INVALID:%s"), Field);
		return false;
	}
	OutValue = static_cast<float>(Number);
	if (!FMath::IsFinite(OutValue))
	{
		OutError = FString::Printf(TEXT("LLMNPC_DRAFT_NUMBER_INVALID:%s"), Field);
		return false;
	}
	return true;
}

bool GetOptionalNumber(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	float DefaultValue,
	float& OutValue,
	FString& OutError
)
{
	OutValue = DefaultValue;
	return !Object->HasField(Field) || GetNumber(Object, Field, OutValue, OutError);
}

bool GetNameArray(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	TArray<FName>& OutNames,
	FString& OutError
)
{
	OutNames.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(Field, Values) || !Values || Values->Num() > 32)
	{
		OutError = FString::Printf(TEXT("LLMNPC_DRAFT_ARRAY_INVALID:%s"), Field);
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Name;
		if (!Value.IsValid() || !Value->TryGetString(Name) || Name.TrimStartAndEnd().IsEmpty())
		{
			OutError = FString::Printf(TEXT("LLMNPC_DRAFT_ARRAY_VALUE_INVALID:%s"), Field);
			return false;
		}
		OutNames.AddUnique(FName(*Name.TrimStartAndEnd()));
	}
	return true;
}

bool GetRange(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FVector2D& OutRange,
	FString& OutError
)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(Field, Values) || !Values || Values->Num() != 2)
	{
		OutError = FString::Printf(TEXT("LLMNPC_DRAFT_RANGE_INVALID:%s"), Field);
		return false;
	}
	double Min = 0.0;
	double Max = 0.0;
	if (
		!(*Values)[0]->TryGetNumber(Min) ||
		!(*Values)[1]->TryGetNumber(Max) ||
		!FMath::IsFinite(Min) ||
		!FMath::IsFinite(Max) ||
		Min <= 0.0 ||
		Max < Min
	)
	{
		OutError = FString::Printf(TEXT("LLMNPC_DRAFT_RANGE_INVALID:%s"), Field);
		return false;
	}
	OutRange = FVector2D(Min, Max);
	return true;
}

bool ParseTrackType(const FString& Value, ELLMMotionTrackType& OutType)
{
	static const TMap<FString, ELLMMotionTrackType> Types = {
		{ TEXT("keyframes"), ELLMMotionTrackType::Keyframes },
		{ TEXT("oscillator"), ELLMMotionTrackType::Oscillator },
		{ TEXT("anchor"), ELLMMotionTrackType::Anchor },
		{ TEXT("look_at"), ELLMMotionTrackType::LookAt },
		{ TEXT("ik_reach"), ELLMMotionTrackType::IKReach },
		{ TEXT("hold"), ELLMMotionTrackType::Hold },
		{ TEXT("spring"), ELLMMotionTrackType::Spring }
	};
	if (const ELLMMotionTrackType* Found = Types.Find(Value))
	{
		OutType = *Found;
		return true;
	}
	return false;
}

bool ParseValueType(const FString& Value, ELLMMotionValueType& OutType)
{
	static const TMap<FString, ELLMMotionValueType> Types = {
		{ TEXT("float"), ELLMMotionValueType::Float },
		{ TEXT("vector"), ELLMMotionValueType::Vector },
		{ TEXT("rotator"), ELLMMotionValueType::Rotator },
		{ TEXT("transform"), ELLMMotionValueType::Transform }
	};
	if (const ELLMMotionValueType* Found = Types.Find(Value))
	{
		OutType = *Found;
		return true;
	}
	return false;
}

bool ParseEnvelope(const FString& Value, ELLMMotionEnvelope& OutEnvelope)
{
	static const TMap<FString, ELLMMotionEnvelope> Envelopes = {
		{ TEXT("none"), ELLMMotionEnvelope::None },
		{ TEXT("smooth"), ELLMMotionEnvelope::Smooth },
		{ TEXT("ease_in"), ELLMMotionEnvelope::EaseIn },
		{ TEXT("ease_out"), ELLMMotionEnvelope::EaseOut },
		{ TEXT("ease_in_out"), ELLMMotionEnvelope::EaseInOut }
	};
	if (const ELLMMotionEnvelope* Found = Envelopes.Find(Value))
	{
		OutEnvelope = *Found;
		return true;
	}
	return false;
}

bool ParseTrack(
	const TSharedPtr<FJsonObject>& Object,
	FLLMMotionTrack& OutTrack,
	FString& OutError
)
{
	static const TSet<FString> Fields = {
		TEXT("control_id"), TEXT("track_type"), TEXT("value_type"),
		TEXT("start_time"), TEXT("end_time"), TEXT("amplitude"),
		TEXT("frequency"), TEXT("phase"), TEXT("envelope"),
		TEXT("strength"), TEXT("reach"), TEXT("target_ref"),
		TEXT("anchor"), TEXT("offset"), TEXT("keys")
	};
	if (!ValidateFields(Object, Fields, TEXT("LLMNPC_DRAFT_TRACK"), OutError))
	{
		return false;
	}

	FString Value;
	if (!GetString(Object, TEXT("control_id"), Value, OutError))
	{
		return false;
	}
	if (!IsSafeIdentifier(Value, 128))
	{
		OutError = TEXT("LLMNPC_DRAFT_CONTROL_ID_INVALID");
		return false;
	}
	OutTrack.ControlId = FName(*Value);
	if (!GetString(Object, TEXT("track_type"), Value, OutError) || !ParseTrackType(Value, OutTrack.TrackType))
	{
		OutError = TEXT("LLMNPC_DRAFT_TRACK_TYPE_INVALID");
		return false;
	}
	if (!GetString(Object, TEXT("value_type"), Value, OutError) || !ParseValueType(Value, OutTrack.ValueType))
	{
		OutError = TEXT("LLMNPC_DRAFT_VALUE_TYPE_INVALID");
		return false;
	}
	if (
		!GetNumber(Object, TEXT("start_time"), OutTrack.StartTime, OutError) ||
		!GetNumber(Object, TEXT("end_time"), OutTrack.EndTime, OutError) ||
		!GetOptionalNumber(Object, TEXT("amplitude"), 0.0f, OutTrack.Amplitude, OutError) ||
		!GetOptionalNumber(Object, TEXT("frequency"), 1.0f, OutTrack.Frequency, OutError) ||
		!GetOptionalNumber(Object, TEXT("phase"), 0.0f, OutTrack.Phase, OutError) ||
		!GetOptionalNumber(Object, TEXT("strength"), 1.0f, OutTrack.Strength, OutError) ||
		!GetOptionalNumber(Object, TEXT("reach"), 0.8f, OutTrack.Reach, OutError)
	)
	{
		return false;
	}
	if (
		OutTrack.StartTime < 0.0f ||
		OutTrack.EndTime <= OutTrack.StartTime ||
		OutTrack.Frequency < 0.0f ||
		OutTrack.Frequency > 8.0f ||
		OutTrack.Strength < 0.0f ||
		OutTrack.Strength > 1.0f ||
		OutTrack.Reach < 0.0f ||
		OutTrack.Reach > 1.0f
	)
	{
		OutError = TEXT("LLMNPC_DRAFT_TRACK_RANGE_INVALID");
		return false;
	}
	if (!GetString(Object, TEXT("envelope"), Value, OutError) || !ParseEnvelope(Value, OutTrack.Envelope))
	{
		OutError = TEXT("LLMNPC_DRAFT_ENVELOPE_INVALID");
		return false;
	}

	Object->TryGetStringField(TEXT("target_ref"), OutTrack.TargetRef);
	OutTrack.TargetRef = OutTrack.TargetRef.TrimStartAndEnd();
	if (OutTrack.TargetRef.Len() > 128)
	{
		OutError = TEXT("LLMNPC_DRAFT_TARGET_REF_TOO_LONG");
		return false;
	}
	if (Object->TryGetStringField(TEXT("anchor"), Value))
	{
		Value = Value.TrimStartAndEnd();
		if (!Value.IsEmpty() && !IsSafeIdentifier(Value, 128))
		{
			OutError = TEXT("LLMNPC_DRAFT_ANCHOR_INVALID");
			return false;
		}
		OutTrack.Anchor = FName(*Value);
	}

	const TArray<TSharedPtr<FJsonValue>>* Offset = nullptr;
	if (Object->TryGetArrayField(TEXT("offset"), Offset) && Offset)
	{
		if (Offset->Num() != 3)
		{
			OutError = TEXT("LLMNPC_DRAFT_TRACK_OFFSET_INVALID");
			return false;
		}
		double X = 0.0;
		double Y = 0.0;
		double Z = 0.0;
		if (
			!(*Offset)[0]->TryGetNumber(X) ||
			!(*Offset)[1]->TryGetNumber(Y) ||
			!(*Offset)[2]->TryGetNumber(Z) ||
			!FMath::IsFinite(X) ||
			!FMath::IsFinite(Y) ||
			!FMath::IsFinite(Z)
		)
		{
			OutError = TEXT("LLMNPC_DRAFT_TRACK_OFFSET_INVALID");
			return false;
		}
		OutTrack.Offset = FVector(X, Y, Z);
	}

	const TArray<TSharedPtr<FJsonValue>>* Keys = nullptr;
	if (Object->TryGetArrayField(TEXT("keys"), Keys) && Keys)
	{
		if (Keys->Num() > 12)
		{
			OutError = TEXT("LLMNPC_DRAFT_TRACK_KEYS_TOO_MANY");
			return false;
		}
		for (const TSharedPtr<FJsonValue>& KeyValue : *Keys)
		{
			const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
			if (!KeyValue.IsValid() || !KeyValue->TryGetArray(Pair) || !Pair || Pair->Num() != 2)
			{
				OutError = TEXT("LLMNPC_DRAFT_TRACK_KEY_INVALID");
				return false;
			}
			double T = 0.0;
			double V = 0.0;
			if (
				!(*Pair)[0]->TryGetNumber(T) ||
				!(*Pair)[1]->TryGetNumber(V) ||
				!FMath::IsFinite(T) ||
				!FMath::IsFinite(V)
			)
			{
				OutError = TEXT("LLMNPC_DRAFT_TRACK_KEY_INVALID");
				return false;
			}
			FLLMMotionKeyFloat& Key = OutTrack.FloatKeys.AddDefaulted_GetRef();
			Key.T = static_cast<float>(T);
			Key.V = static_cast<float>(V);
		}
	}
	if (OutTrack.TrackType == ELLMMotionTrackType::Keyframes && OutTrack.FloatKeys.IsEmpty())
	{
		OutError = TEXT("LLMNPC_DRAFT_KEYFRAME_TRACK_EMPTY");
		return false;
	}
	float PreviousKeyTime = -1.0f;
	for (const FLLMMotionKeyFloat& Key : OutTrack.FloatKeys)
	{
		if (
			Key.T < OutTrack.StartTime ||
			Key.T > OutTrack.EndTime ||
			Key.T <= PreviousKeyTime
		)
		{
			OutError = TEXT("LLMNPC_DRAFT_TRACK_KEY_TIME_INVALID");
			return false;
		}
		PreviousKeyTime = Key.T;
	}
	return true;
}

bool ParseProvenance(
	const TSharedPtr<FJsonObject>& Provenance,
	FLLMNPCParsedDraftInfo& OutInfo,
	FString& OutJson,
	FString& OutError
)
{
	static const TSet<FString> Fields = {
		TEXT("source_type"), TEXT("source_sequence_path"),
		TEXT("reconstruction_profile_uri"), TEXT("reconstruction_profile_path"),
		TEXT("reconstruction_profile_hash"), TEXT("full_pose_artifact_uri"),
		TEXT("full_pose_artifact_hash"), TEXT("source_license"),
		TEXT("authoring_agent")
	};
	if (!ValidateFields(Provenance, Fields, TEXT("LLMNPC_DRAFT_PROVENANCE"), OutError))
	{
		return false;
	}
	FString SourceType;
	if (
		!GetString(Provenance, TEXT("source_type"), SourceType, OutError) ||
		!GetString(Provenance, TEXT("source_sequence_path"), OutInfo.SourceSequencePath, OutError) ||
		!GetString(Provenance, TEXT("reconstruction_profile_hash"), OutInfo.ReconstructionProfileHash, OutError)
	)
	{
		return false;
	}
	if (
		SourceType != TEXT("uepi_reconstruction_profile") &&
		SourceType != TEXT("manual_reference") &&
		SourceType != TEXT("project_owned_reference")
	)
	{
		OutError = TEXT("LLMNPC_DRAFT_PROVENANCE_SOURCE_TYPE_INVALID");
		return false;
	}

	const TSharedPtr<FJsonObject>* License = nullptr;
	const TSharedPtr<FJsonObject>* Agent = nullptr;
	if (
		!Provenance->TryGetObjectField(TEXT("source_license"), License) || !License || !License->IsValid() ||
		!Provenance->TryGetObjectField(TEXT("authoring_agent"), Agent) || !Agent || !Agent->IsValid()
	)
	{
		OutError = TEXT("LLMNPC_DRAFT_PROVENANCE_NESTED_OBJECT_MISSING");
		return false;
	}
	static const TSet<FString> LicenseFields = {
		TEXT("identifier"), TEXT("holder"), TEXT("redistribution_allowed"), TEXT("notes")
	};
	static const TSet<FString> AgentFields = {
		TEXT("tool"), TEXT("version"), TEXT("prompt_version")
	};
	if (
		!ValidateFields(*License, LicenseFields, TEXT("LLMNPC_DRAFT_LICENSE"), OutError) ||
		!ValidateFields(*Agent, AgentFields, TEXT("LLMNPC_DRAFT_AGENT"), OutError)
	)
	{
		return false;
	}
	FString Value;
	bool bRedistributionAllowed = false;
	if (
		!GetString(*License, TEXT("identifier"), Value, OutError) ||
		!GetString(*License, TEXT("holder"), Value, OutError) ||
		!GetBool(*License, TEXT("redistribution_allowed"), bRedistributionAllowed, OutError) ||
		!GetString(*Agent, TEXT("tool"), Value, OutError) ||
		!GetString(*Agent, TEXT("version"), Value, OutError) ||
		!GetString(*Agent, TEXT("prompt_version"), Value, OutError)
	)
	{
		return false;
	}

	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	if (!FJsonSerializer::Serialize(Provenance.ToSharedRef(), Writer))
	{
		OutError = TEXT("LLMNPC_DRAFT_PROVENANCE_SERIALIZE_FAILED");
		return false;
	}
	return true;
}
}

bool FLLMNPCTemplateDraftImporter::ParseDraftJson(
	const FString& DraftJson,
	ULLMNPCMotionTemplate& OutTemplate,
	FLLMNPCParsedDraftInfo& OutInfo,
	FString& OutError
)
{
	OutInfo = FLLMNPCParsedDraftInfo();
	OutError.Reset();

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DraftJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_DRAFT_JSON_INVALID");
		return false;
	}
	static const TSet<FString> RootFields = {
		TEXT("schema_version"), TEXT("asset_name"), TEXT("template_id"),
		TEXT("public_action_id"), TEXT("semantic_version"), TEXT("kind"),
		TEXT("variant_id"), TEXT("variant_weight"), TEXT("variant_style_tags"),
		TEXT("review_state"), TEXT("display_name"), TEXT("description"),
		TEXT("skeleton_profile_id"), TEXT("metadata"), TEXT("modifier_policy"),
		TEXT("clip"), TEXT("provenance")
	};
	if (!ValidateFields(Root, RootFields, TEXT("LLMNPC_DRAFT_ROOT"), OutError))
	{
		return false;
	}

	FString Value;
	if (!GetString(Root, TEXT("schema_version"), Value, OutError) || Value != TEXT("llmnpc.motion_template_draft.v1"))
	{
		OutError = TEXT("LLMNPC_DRAFT_SCHEMA_UNSUPPORTED");
		return false;
	}
	if (
		!GetString(Root, TEXT("asset_name"), OutInfo.AssetName, OutError) ||
		!GetString(Root, TEXT("template_id"), Value, OutError)
	)
	{
		return false;
	}
	if (
		!IsSafeIdentifier(OutInfo.AssetName, 128) ||
		!FChar::IsAlpha(OutInfo.AssetName[0]) ||
		OutInfo.AssetName.Contains(TEXT(".")) ||
		OutInfo.AssetName.Contains(TEXT("-"))
	)
	{
		OutError = TEXT("LLMNPC_DRAFT_ASSET_NAME_INVALID");
		return false;
	}
	if (!IsSafeIdentifier(Value, 128))
	{
		OutError = TEXT("LLMNPC_DRAFT_TEMPLATE_ID_INVALID");
		return false;
	}
	OutTemplate.Metadata.TemplateId = FName(*Value);
	if (!GetString(Root, TEXT("public_action_id"), Value, OutError))
	{
		return false;
	}
	if (!IsSafeIdentifier(Value, 128))
	{
		OutError = TEXT("LLMNPC_DRAFT_PUBLIC_ACTION_ID_INVALID");
		return false;
	}
	OutTemplate.Metadata.PublicActionId = FName(*Value);
	if (!GetString(Root, TEXT("semantic_version"), OutTemplate.Metadata.SemanticVersion, OutError))
	{
		return false;
	}
	if (!IsSemanticVersion(OutTemplate.Metadata.SemanticVersion))
	{
		OutError = TEXT("LLMNPC_DRAFT_SEMANTIC_VERSION_INVALID");
		return false;
	}
	if (Root->HasField(TEXT("variant_id")))
	{
		if (!GetString(Root, TEXT("variant_id"), Value, OutError) || !IsSafeIdentifier(Value, 64))
		{
			OutError = TEXT("LLMNPC_DRAFT_VARIANT_ID_INVALID");
			return false;
		}
		OutTemplate.Metadata.VariantId = FName(*Value);
	}
	if (!GetOptionalNumber(Root, TEXT("variant_weight"), 1.0f, OutTemplate.Metadata.VariantWeight, OutError))
	{
		return false;
	}
	if (Root->HasField(TEXT("variant_style_tags")) && !GetNameArray(
		Root,
		TEXT("variant_style_tags"),
		OutTemplate.Metadata.VariantStyleTags,
		OutError
	))
	{
		return false;
	}
	if (!GetString(Root, TEXT("kind"), Value, OutError) || Value != TEXT("procedural_motion"))
	{
		OutError = TEXT("LLMNPC_DRAFT_KIND_UNSUPPORTED");
		return false;
	}
	OutTemplate.Kind = ELLMNPCTemplateKind::ProceduralMotion;
	if (
		!GetString(Root, TEXT("review_state"), Value, OutError) ||
		(Value != TEXT("draft") && Value != TEXT("generated"))
	)
	{
		OutError = TEXT("LLMNPC_DRAFT_REVIEW_STATE_FORBIDDEN");
		return false;
	}
	OutTemplate.Metadata.ReviewState = ELLMNPCTemplateReviewState::Generated;

	FString TextValue;
	if (!GetString(Root, TEXT("display_name"), TextValue, OutError))
	{
		return false;
	}
	if (TextValue.Len() > 128)
	{
		OutError = TEXT("LLMNPC_DRAFT_DISPLAY_NAME_TOO_LONG");
		return false;
	}
	OutTemplate.Metadata.DisplayName = FText::FromString(TextValue);
	if (!GetString(Root, TEXT("description"), TextValue, OutError))
	{
		return false;
	}
	if (TextValue.Len() > 1000)
	{
		OutError = TEXT("LLMNPC_DRAFT_DESCRIPTION_TOO_LONG");
		return false;
	}
	OutTemplate.Metadata.Description = FText::FromString(TextValue);
	if (!GetString(Root, TEXT("skeleton_profile_id"), Value, OutError))
	{
		return false;
	}
	if (!IsSafeIdentifier(Value, 128))
	{
		OutError = TEXT("LLMNPC_DRAFT_SKELETON_PROFILE_ID_INVALID");
		return false;
	}
	OutTemplate.Metadata.SkeletonProfileId = FName(*Value);

	const TSharedPtr<FJsonObject>* Metadata = nullptr;
	if (!Root->TryGetObjectField(TEXT("metadata"), Metadata) || !Metadata || !Metadata->IsValid())
	{
		OutError = TEXT("LLMNPC_DRAFT_METADATA_MISSING");
		return false;
	}
	static const TSet<FString> MetadataFields = {
		TEXT("intent_tags"), TEXT("emotion_tags"), TEXT("personality_tags"),
		TEXT("required_channels"), TEXT("blocked_states"), TEXT("requires_target"),
		TEXT("can_run_while_moving"), TEXT("allow_runtime_model_selection"),
		TEXT("cooldown_seconds")
	};
	if (!ValidateFields(*Metadata, MetadataFields, TEXT("LLMNPC_DRAFT_METADATA"), OutError))
	{
		return false;
	}
	if (
		!GetNameArray(*Metadata, TEXT("intent_tags"), OutTemplate.Metadata.IntentTags, OutError) ||
		!GetNameArray(*Metadata, TEXT("emotion_tags"), OutTemplate.Metadata.EmotionTags, OutError) ||
		!GetNameArray(*Metadata, TEXT("personality_tags"), OutTemplate.Metadata.PersonalityTags, OutError) ||
		!GetNameArray(*Metadata, TEXT("required_channels"), OutTemplate.Metadata.RequiredChannels, OutError) ||
		!GetNameArray(*Metadata, TEXT("blocked_states"), OutTemplate.Metadata.BlockedStates, OutError) ||
		!GetBool(*Metadata, TEXT("requires_target"), OutTemplate.Metadata.bRequiresTarget, OutError) ||
		!GetBool(*Metadata, TEXT("can_run_while_moving"), OutTemplate.Metadata.bCanRunWhileMoving, OutError) ||
		!GetBool(*Metadata, TEXT("allow_runtime_model_selection"), OutTemplate.Metadata.bAllowRuntimeModelSelection, OutError) ||
		!GetNumber(*Metadata, TEXT("cooldown_seconds"), OutTemplate.Metadata.CooldownSeconds, OutError)
	)
	{
		return false;
	}
	if (OutTemplate.Metadata.CooldownSeconds < 0.0f || OutTemplate.Metadata.CooldownSeconds > 60.0f)
	{
		OutError = TEXT("LLMNPC_DRAFT_COOLDOWN_INVALID");
		return false;
	}

	const TSharedPtr<FJsonObject>* ModifierPolicy = nullptr;
	if (!Root->TryGetObjectField(TEXT("modifier_policy"), ModifierPolicy) || !ModifierPolicy || !ModifierPolicy->IsValid())
	{
		OutError = TEXT("LLMNPC_DRAFT_MODIFIER_POLICY_MISSING");
		return false;
	}
	static const TSet<FString> ModifierFields = {
		TEXT("amplitude"), TEXT("speed"), TEXT("duration"),
		TEXT("allow_mirror"), TEXT("allowed_styles"),
		TEXT("random_amplitude_jitter"), TEXT("random_speed_jitter"),
		TEXT("random_frequency_jitter"), TEXT("random_phase_jitter_radians")
	};
	if (!ValidateFields(*ModifierPolicy, ModifierFields, TEXT("LLMNPC_DRAFT_MODIFIER"), OutError))
	{
		return false;
	}
	if (
		!GetRange(*ModifierPolicy, TEXT("amplitude"), OutTemplate.ModifierPolicy.AmplitudeRange, OutError) ||
		!GetRange(*ModifierPolicy, TEXT("speed"), OutTemplate.ModifierPolicy.SpeedRange, OutError) ||
		!GetRange(*ModifierPolicy, TEXT("duration"), OutTemplate.ModifierPolicy.DurationRange, OutError) ||
		!GetBool(*ModifierPolicy, TEXT("allow_mirror"), OutTemplate.ModifierPolicy.bAllowMirror, OutError) ||
		!GetNameArray(*ModifierPolicy, TEXT("allowed_styles"), OutTemplate.ModifierPolicy.AllowedStyleTags, OutError) ||
		!GetOptionalNumber(*ModifierPolicy, TEXT("random_amplitude_jitter"), 0.03f, OutTemplate.ModifierPolicy.RandomAmplitudeJitter, OutError) ||
		!GetOptionalNumber(*ModifierPolicy, TEXT("random_speed_jitter"), 0.025f, OutTemplate.ModifierPolicy.RandomSpeedJitter, OutError) ||
		!GetOptionalNumber(*ModifierPolicy, TEXT("random_frequency_jitter"), 0.04f, OutTemplate.ModifierPolicy.RandomFrequencyJitter, OutError) ||
		!GetOptionalNumber(*ModifierPolicy, TEXT("random_phase_jitter_radians"), 0.08f, OutTemplate.ModifierPolicy.RandomPhaseJitterRadians, OutError)
	)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Clip = nullptr;
	if (!Root->TryGetObjectField(TEXT("clip"), Clip) || !Clip || !Clip->IsValid())
	{
		OutError = TEXT("LLMNPC_DRAFT_CLIP_MISSING");
		return false;
	}
	static const TSet<FString> ClipFields = {
		TEXT("clip_id"), TEXT("duration"), TEXT("blend_in"), TEXT("blend_out"),
		TEXT("priority"), TEXT("interruptible"), TEXT("tracks")
	};
	if (!ValidateFields(*Clip, ClipFields, TEXT("LLMNPC_DRAFT_CLIP"), OutError))
	{
		return false;
	}
	if (
		!GetString(*Clip, TEXT("clip_id"), OutTemplate.ProceduralClip.ClipId, OutError) ||
		!GetNumber(*Clip, TEXT("duration"), OutTemplate.ProceduralClip.Duration, OutError) ||
		!GetNumber(*Clip, TEXT("blend_in"), OutTemplate.ProceduralClip.BlendIn, OutError) ||
		!GetNumber(*Clip, TEXT("blend_out"), OutTemplate.ProceduralClip.BlendOut, OutError) ||
		!GetNumber(*Clip, TEXT("priority"), OutTemplate.ProceduralClip.Priority, OutError) ||
		!GetBool(*Clip, TEXT("interruptible"), OutTemplate.ProceduralClip.bInterruptible, OutError)
	)
	{
		return false;
	}
	if (
		OutTemplate.ProceduralClip.Duration < 0.05f ||
		OutTemplate.ProceduralClip.Duration > 3.0f ||
		OutTemplate.ProceduralClip.BlendIn < 0.0f ||
		OutTemplate.ProceduralClip.BlendIn > 1.0f ||
		OutTemplate.ProceduralClip.BlendOut < 0.0f ||
		OutTemplate.ProceduralClip.BlendOut > 1.0f ||
		OutTemplate.ProceduralClip.Priority < 0.0f ||
		OutTemplate.ProceduralClip.Priority > 1.0f
	)
	{
		OutError = TEXT("LLMNPC_DRAFT_CLIP_RANGE_INVALID");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Tracks = nullptr;
	if (!(*Clip)->TryGetArrayField(TEXT("tracks"), Tracks) || !Tracks || Tracks->IsEmpty() || Tracks->Num() > 24)
	{
		OutError = TEXT("LLMNPC_DRAFT_TRACKS_INVALID");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& TrackValue : *Tracks)
	{
		FLLMMotionTrack& Track = OutTemplate.ProceduralClip.Tracks.AddDefaulted_GetRef();
		if (!ParseTrack(TrackValue->AsObject(), Track, OutError))
		{
			return false;
		}
		if (Track.EndTime > OutTemplate.ProceduralClip.Duration)
		{
			OutError = TEXT("LLMNPC_DRAFT_TRACK_EXCEEDS_CLIP");
			return false;
		}
	}

	const TSharedPtr<FJsonObject>* Provenance = nullptr;
	if (!Root->TryGetObjectField(TEXT("provenance"), Provenance) || !Provenance || !Provenance->IsValid())
	{
		OutError = TEXT("LLMNPC_DRAFT_PROVENANCE_MISSING");
		return false;
	}
	if (!ParseProvenance(*Provenance, OutInfo, OutTemplate.SourceProvenanceJson, OutError))
	{
		return false;
	}

	FString TemplateError;
	if (!OutTemplate.ValidateTemplate(TemplateError))
	{
		OutError = TemplateError;
		return false;
	}
	return true;
}
