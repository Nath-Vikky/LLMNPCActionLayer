#include "Authoring/LLMNPCAnimationTemplateDraftImporter.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "UObject/Package.h"

namespace LLMNPCAnimationDraftPrivate
{
bool IsSafeIdentifier(const FString& Value, int32 MaxLength)
{
	if (
		Value.IsEmpty() ||
		Value.Len() > MaxLength ||
		Value.Contains(TEXT("/")) ||
		Value.Contains(TEXT("\\"))
	)
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
			OutError = FString::Printf(
				TEXT("%s_FIELD_UNKNOWN:%s"),
				Prefix,
				*Pair.Key
			);
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
		OutError = FString::Printf(
			TEXT("LLMNPC_ANIMATION_DRAFT_FIELD_MISSING:%s"),
			Field
		);
		return false;
	}
	OutValue = OutValue.TrimStartAndEnd();
	if (!bAllowEmpty && OutValue.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_ANIMATION_DRAFT_FIELD_EMPTY:%s"),
			Field
		);
		return false;
	}
	return true;
}

bool GetOptionalString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	int32 MaxLength,
	FString& OutValue,
	FString& OutError
)
{
	OutValue.Reset();
	if (!Object.IsValid() || !Object->HasField(Field))
	{
		return true;
	}
	if (
		!Object->TryGetStringField(Field, OutValue) ||
		OutValue.Len() > MaxLength
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_ANIMATION_DRAFT_STRING_INVALID:%s"),
			Field
		);
		return false;
	}
	OutValue = OutValue.TrimStartAndEnd();
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
		OutError = FString::Printf(
			TEXT("LLMNPC_ANIMATION_DRAFT_BOOL_INVALID:%s"),
			Field
		);
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
		OutError = FString::Printf(
			TEXT("LLMNPC_ANIMATION_DRAFT_NUMBER_INVALID:%s"),
			Field
		);
		return false;
	}
	OutValue = static_cast<float>(Number);
	if (!FMath::IsFinite(OutValue))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_ANIMATION_DRAFT_NUMBER_INVALID:%s"),
			Field
		);
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
	return !Object->HasField(Field) ||
		GetNumber(Object, Field, OutValue, OutError);
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
	if (
		!Object.IsValid() ||
		!Object->TryGetArrayField(Field, Values) ||
		!Values ||
		Values->Num() > 32
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_ANIMATION_DRAFT_ARRAY_INVALID:%s"),
			Field
		);
		return false;
	}
	for (const TSharedPtr<FJsonValue>& JsonValue : *Values)
	{
		FString Value;
		if (
			!JsonValue.IsValid() ||
			!JsonValue->TryGetString(Value)
		)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_ANIMATION_DRAFT_ARRAY_VALUE_INVALID:%s"),
				Field
			);
			return false;
		}
		Value = Value.TrimStartAndEnd();
		const FName Name(*Value);
		if (
			Value.IsEmpty() ||
			!IsSafeIdentifier(Value, 128) ||
			Name.IsNone() ||
			OutNames.Contains(Name)
		)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_ANIMATION_DRAFT_ARRAY_VALUE_INVALID:%s"),
				Field
			);
			return false;
		}
		OutNames.Add(Name);
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
	if (
		!Object.IsValid() ||
		!Object->TryGetArrayField(Field, Values) ||
		!Values ||
		Values->Num() != 2
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_ANIMATION_DRAFT_RANGE_INVALID:%s"),
			Field
		);
		return false;
	}
	double Minimum = 0.0;
	double Maximum = 0.0;
	if (
		!(*Values)[0]->TryGetNumber(Minimum) ||
		!(*Values)[1]->TryGetNumber(Maximum) ||
		!FMath::IsFinite(Minimum) ||
		!FMath::IsFinite(Maximum) ||
		Minimum <= 0.0 ||
		Maximum < Minimum ||
		Maximum > 4.0
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_ANIMATION_DRAFT_RANGE_INVALID:%s"),
			Field
		);
		return false;
	}
	OutRange = FVector2D(Minimum, Maximum);
	return true;
}

bool SerializeJsonObject(
	const TSharedRef<FJsonObject>& Object,
	FString& OutJson
)
{
	OutJson.Reset();
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Object, Writer);
}

bool ParseProvenance(
	const TSharedPtr<FJsonObject>& Provenance,
	const UAnimationAsset& SelectedAnimationAsset,
	FLLMNPCParsedAnimationDraftInfo& OutInfo,
	FString& OutJson,
	FString& OutError
)
{
	static const TSet<FString> ProvenanceFields = {
		TEXT("source_type"),
		TEXT("source_notes"),
		TEXT("reconstruction_profile_uri"),
		TEXT("reconstruction_profile_path"),
		TEXT("reconstruction_profile_hash"),
		TEXT("full_pose_artifact_uri"),
		TEXT("full_pose_artifact_hash"),
		TEXT("source_license"),
		TEXT("authoring_agent")
	};
	if (!ValidateFields(
		Provenance,
		ProvenanceFields,
		TEXT("LLMNPC_ANIMATION_DRAFT_PROVENANCE"),
		OutError
	))
	{
		return false;
	}

	FString SourceType;
	if (
		!GetString(
			Provenance,
			TEXT("source_type"),
			SourceType,
			OutError
		) ||
		(
			SourceType != TEXT("project_owned_animation_asset") &&
			SourceType != TEXT("licensed_animation_asset")
		)
	)
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_SOURCE_TYPE_INVALID");
		return false;
	}

	FString SourceNotes;
	FString ReconstructionProfileUri;
	FString ReconstructionProfilePath;
	FString FullPoseArtifactUri;
	FString FullPoseArtifactHash;
	if (
		!GetOptionalString(
			Provenance,
			TEXT("source_notes"),
			1000,
			SourceNotes,
			OutError
		) ||
		!GetOptionalString(
			Provenance,
			TEXT("reconstruction_profile_uri"),
			2048,
			ReconstructionProfileUri,
			OutError
		) ||
		!GetOptionalString(
			Provenance,
			TEXT("reconstruction_profile_path"),
			2048,
			ReconstructionProfilePath,
			OutError
		) ||
		!GetOptionalString(
			Provenance,
			TEXT("full_pose_artifact_uri"),
			2048,
			FullPoseArtifactUri,
			OutError
		) ||
		!GetOptionalString(
			Provenance,
			TEXT("full_pose_artifact_hash"),
			256,
			FullPoseArtifactHash,
			OutError
		)
	)
	{
		return false;
	}
	if (
		!FullPoseArtifactHash.IsEmpty() &&
		FullPoseArtifactHash.Len() < 8
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_FULL_POSE_HASH_INVALID");
		return false;
	}

	const TSharedPtr<FJsonObject>* License = nullptr;
	if (
		!Provenance->TryGetObjectField(TEXT("source_license"), License) ||
		!License ||
		!License->IsValid()
	)
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_LICENSE_MISSING");
		return false;
	}
	static const TSet<FString> LicenseFields = {
		TEXT("identifier"),
		TEXT("holder"),
		TEXT("redistribution_allowed"),
		TEXT("notes")
	};
	if (!ValidateFields(
		*License,
		LicenseFields,
		TEXT("LLMNPC_ANIMATION_DRAFT_LICENSE"),
		OutError
	))
	{
		return false;
	}
	FString LicenseIdentifier;
	FString LicenseHolder;
	bool bRedistributionAllowed = false;
	if (
		!GetString(
			*License,
			TEXT("identifier"),
			LicenseIdentifier,
			OutError
		) ||
		!GetString(
			*License,
			TEXT("holder"),
			LicenseHolder,
			OutError
		) ||
		!GetBool(
			*License,
			TEXT("redistribution_allowed"),
			bRedistributionAllowed,
			OutError
		)
	)
	{
		return false;
	}
	if (
		LicenseIdentifier.Len() > 256 ||
		LicenseHolder.Len() > 256
	)
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_LICENSE_INVALID");
		return false;
	}
	FString LicenseNotes;
	if (!GetOptionalString(
		*License,
		TEXT("notes"),
		1000,
		LicenseNotes,
		OutError
	))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Agent = nullptr;
	if (
		!Provenance->TryGetObjectField(TEXT("authoring_agent"), Agent) ||
		!Agent ||
		!Agent->IsValid()
	)
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_AUTHORING_AGENT_MISSING");
		return false;
	}
	static const TSet<FString> AgentFields = {
		TEXT("tool"),
		TEXT("version"),
		TEXT("prompt_version")
	};
	if (!ValidateFields(
		*Agent,
		AgentFields,
		TEXT("LLMNPC_ANIMATION_DRAFT_AUTHORING_AGENT"),
		OutError
	))
	{
		return false;
	}
	FString StringValue;
	if (
		!GetString(*Agent, TEXT("tool"), StringValue, OutError) ||
		StringValue.Len() > 256 ||
		!GetString(*Agent, TEXT("version"), StringValue, OutError) ||
		StringValue.Len() > 256 ||
		!GetString(*Agent, TEXT("prompt_version"), StringValue, OutError) ||
		StringValue.Len() > 256
	)
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("LLMNPC_ANIMATION_DRAFT_AUTHORING_AGENT_INVALID");
		}
		return false;
	}

	if (!GetOptionalString(
		Provenance,
		TEXT("reconstruction_profile_hash"),
		256,
		OutInfo.ReconstructionProfileHash,
		OutError
	))
	{
		return false;
	}
	if (
		!OutInfo.ReconstructionProfileHash.IsEmpty() &&
		OutInfo.ReconstructionProfileHash.Len() < 8
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_RECONSTRUCTION_HASH_INVALID");
		return false;
	}

	if (
		!FLLMNPCAnimationTemplateDraftImporter::
			BuildAnimationAssetPackageHash(
				SelectedAnimationAsset,
				OutInfo.SelectedAnimationAssetPackageHash,
				OutError
			)
	)
	{
		return false;
	}
	OutInfo.SelectedAnimationAssetPath =
		SelectedAnimationAsset.GetPathName();
	Provenance->SetStringField(
		TEXT("source_asset_path"),
		OutInfo.SelectedAnimationAssetPath
	);
	Provenance->SetStringField(
		TEXT("source_asset_class"),
		SelectedAnimationAsset.GetClass()->GetPathName()
	);
	Provenance->SetStringField(
		TEXT("source_asset_package_hash"),
		OutInfo.SelectedAnimationAssetPackageHash
	);
	Provenance->SetStringField(
		TEXT("source_skeleton_path"),
		SelectedAnimationAsset.GetSkeleton()
			? SelectedAnimationAsset.GetSkeleton()->GetPathName()
			: FString()
	);
	TSharedRef<FJsonObject> Selection = MakeShared<FJsonObject>();
	Selection->SetStringField(
		TEXT("method"),
		TEXT("ue_asset_picker")
	);
	Selection->SetBoolField(
		TEXT("model_supplied_asset_path"),
		false
	);
	Provenance->SetObjectField(
		TEXT("asset_selection"),
		Selection
	);
	if (!SerializeJsonObject(Provenance.ToSharedRef(), OutJson))
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_PROVENANCE_SERIALIZE_FAILED");
		return false;
	}
	return true;
}
}

bool FLLMNPCAnimationTemplateDraftImporter::BuildAnimationAssetPackageHash(
	const UAnimationAsset& AnimationAsset,
	FString& OutPackageHash,
	FString& OutError
)
{
	OutPackageHash.Reset();
	OutError.Reset();
	const UPackage* Package = AnimationAsset.GetOutermost();
	if (
		!Package ||
		Package == GetTransientPackage() ||
		Package->IsDirty() ||
		!FPackageName::IsValidLongPackageName(Package->GetName())
	)
	{
		OutError = Package && Package->IsDirty()
			? TEXT("LLMNPC_ANIMATION_SOURCE_ASSET_SAVE_REQUIRED")
			: TEXT("LLMNPC_ANIMATION_SOURCE_PACKAGE_INVALID");
		return false;
	}

	const FString MainFilename =
		FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension()
		);
	if (!IFileManager::Get().FileExists(*MainFilename))
	{
		OutError = TEXT("LLMNPC_ANIMATION_SOURCE_PACKAGE_FILE_MISSING");
		return false;
	}

	const FString MainExtension =
		FPaths::GetExtension(MainFilename, true);
	const FString PackageBase =
		MainFilename.LeftChop(MainExtension.Len());
	TArray<FString> PackageFiles = {
		MainFilename,
		PackageBase + TEXT(".uexp"),
		PackageBase + TEXT(".ubulk"),
		PackageBase + TEXT(".uptnl")
	};
	TArray<FString> HashManifest;
	for (const FString& PackageFile : PackageFiles)
	{
		if (!IFileManager::Get().FileExists(*PackageFile))
		{
			continue;
		}
		HashManifest.Add(
			FString::Printf(
				TEXT("%s:%s"),
				*FPaths::GetExtension(PackageFile, true).ToLower(),
				*LexToString(FMD5Hash::HashFile(*PackageFile)).ToLower()
			)
		);
	}
	HashManifest.Sort();
	if (HashManifest.IsEmpty())
	{
		OutError = TEXT("LLMNPC_ANIMATION_SOURCE_PACKAGE_HASH_FAILED");
		return false;
	}
	OutPackageHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(
			*FString::Join(HashManifest, TEXT("\n"))
		).ToLower()
	);
	return true;
}

bool FLLMNPCAnimationTemplateDraftImporter::ParseDraftJson(
	const FString& DraftJson,
	const UAnimationAsset& SelectedAnimationAsset,
	ULLMNPCMotionTemplate& OutTemplate,
	FLLMNPCParsedAnimationDraftInfo& OutInfo,
	FString& OutError
)
{
	using namespace LLMNPCAnimationDraftPrivate;

	OutInfo = FLLMNPCParsedAnimationDraftInfo();
	OutError.Reset();
	OutTemplate.Kind = ELLMNPCTemplateKind::AnimationAsset;
	OutTemplate.Metadata = FLLMNPCTemplateMetadata();
	OutTemplate.ModifierPolicy = FLLMNPCModifierPolicy();
	OutTemplate.ProceduralClip = FLLMMotionClip();
	OutTemplate.AnimationAsset.Reset();
	OutTemplate.AnimationPlayback = FLLMNPCAnimationPlaybackPolicy();
	OutTemplate.SourceProvenanceJson.Reset();
	OutTemplate.ValidationReportJson.Reset();
	if (DraftJson.IsEmpty() || DraftJson.Len() > 512 * 1024)
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_SIZE_INVALID");
		return false;
	}

	const UAnimSequenceBase* Sequence =
		Cast<UAnimSequenceBase>(&SelectedAnimationAsset);
	if (!Sequence)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_ASSET_TYPE_UNSUPPORTED");
		return false;
	}
	if (
		SelectedAnimationAsset.IsUnreachable() ||
		SelectedAnimationAsset.GetPathName().IsEmpty()
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_ASSET_UNAVAILABLE");
		return false;
	}
	if (!SelectedAnimationAsset.GetSkeleton())
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_ASSET_SKELETON_MISSING");
		return false;
	}
	if (
		!FMath::IsFinite(Sequence->GetPlayLength()) ||
		Sequence->GetPlayLength() <= 0.0f
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_ASSET_DURATION_INVALID");
		return false;
	}
	if (Sequence->HasRootMotion())
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_ROOT_MOTION_SOURCE_FORBIDDEN");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(DraftJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_JSON_INVALID");
		return false;
	}
	static const TSet<FString> RootFields = {
		TEXT("schema_version"),
		TEXT("asset_name"),
		TEXT("template_id"),
		TEXT("public_action_id"),
		TEXT("semantic_version"),
		TEXT("kind"),
		TEXT("variant_id"),
		TEXT("variant_weight"),
		TEXT("variant_style_tags"),
		TEXT("review_state"),
		TEXT("display_name"),
		TEXT("description"),
		TEXT("skeleton_profile_id"),
		TEXT("compatible_skeleton_profile_ids"),
		TEXT("metadata"),
		TEXT("modifier_policy"),
		TEXT("playback"),
		TEXT("provenance")
	};
	if (!ValidateFields(
		Root,
		RootFields,
		TEXT("LLMNPC_ANIMATION_DRAFT_ROOT"),
		OutError
	))
	{
		return false;
	}

	FString Value;
	if (
		!GetString(Root, TEXT("schema_version"), Value, OutError) ||
		Value != TEXT("llmnpc.animation_template_draft.v1")
	)
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_SCHEMA_UNSUPPORTED");
		return false;
	}
	if (
		!GetString(
			Root,
			TEXT("asset_name"),
			OutInfo.AssetName,
			OutError
		) ||
		!IsSafeIdentifier(OutInfo.AssetName, 128) ||
		!FChar::IsAlpha(OutInfo.AssetName[0]) ||
		OutInfo.AssetName.Contains(TEXT(".")) ||
		OutInfo.AssetName.Contains(TEXT("-"))
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_ASSET_NAME_INVALID");
		return false;
	}
	if (
		!GetString(Root, TEXT("template_id"), Value, OutError) ||
		!IsSafeIdentifier(Value, 128)
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_TEMPLATE_ID_INVALID");
		return false;
	}
	OutTemplate.Metadata.TemplateId = FName(*Value);
	if (
		!GetString(Root, TEXT("public_action_id"), Value, OutError) ||
		!IsSafeIdentifier(Value, 128)
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_PUBLIC_ACTION_ID_INVALID");
		return false;
	}
	OutTemplate.Metadata.PublicActionId = FName(*Value);
	if (
		!GetString(
			Root,
			TEXT("semantic_version"),
			OutTemplate.Metadata.SemanticVersion,
			OutError
		) ||
		!IsSemanticVersion(OutTemplate.Metadata.SemanticVersion)
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_SEMANTIC_VERSION_INVALID");
		return false;
	}
	if (
		!GetString(Root, TEXT("kind"), Value, OutError) ||
		Value != TEXT("animation_asset")
	)
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_KIND_UNSUPPORTED");
		return false;
	}
	if (
		!GetString(Root, TEXT("review_state"), Value, OutError) ||
		(Value != TEXT("draft") && Value != TEXT("generated"))
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_REVIEW_STATE_FORBIDDEN");
		return false;
	}
	OutTemplate.Metadata.ReviewState =
		ELLMNPCTemplateReviewState::Generated;

	if (
		!GetString(Root, TEXT("variant_id"), Value, OutError) ||
		!IsSafeIdentifier(Value, 64)
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_VARIANT_ID_INVALID");
		return false;
	}
	OutTemplate.Metadata.VariantId = FName(*Value);
	if (
		!GetNumber(
			Root,
			TEXT("variant_weight"),
			OutTemplate.Metadata.VariantWeight,
			OutError
		) ||
		OutTemplate.Metadata.VariantWeight < 0.01f ||
		OutTemplate.Metadata.VariantWeight > 100.0f ||
		!GetNameArray(
			Root,
			TEXT("variant_style_tags"),
			OutTemplate.Metadata.VariantStyleTags,
			OutError
		)
	)
	{
		if (OutError.IsEmpty())
		{
			OutError =
				TEXT("LLMNPC_ANIMATION_DRAFT_VARIANT_WEIGHT_INVALID");
		}
		return false;
	}

	FString TextValue;
	if (
		!GetString(Root, TEXT("display_name"), TextValue, OutError) ||
		TextValue.Len() > 128
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_DISPLAY_NAME_INVALID");
		return false;
	}
	OutTemplate.Metadata.DisplayName = FText::FromString(TextValue);
	if (
		!GetString(Root, TEXT("description"), TextValue, OutError) ||
		TextValue.Len() > 1000
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_DESCRIPTION_INVALID");
		return false;
	}
	OutTemplate.Metadata.Description = FText::FromString(TextValue);
	if (
		!GetString(Root, TEXT("skeleton_profile_id"), Value, OutError) ||
		!IsSafeIdentifier(Value, 128)
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_SKELETON_PROFILE_ID_INVALID");
		return false;
	}
	OutTemplate.Metadata.SkeletonProfileId = FName(*Value);
	if (
		Root->HasField(TEXT("compatible_skeleton_profile_ids")) &&
		!GetNameArray(
			Root,
			TEXT("compatible_skeleton_profile_ids"),
			OutTemplate.Metadata.CompatibleSkeletonProfileIds,
			OutError
		)
	)
	{
		return false;
	}
	OutTemplate.Metadata.CatalogSchemaVersion =
		LLMNPCCatalog::SchemaVersion;
	OutTemplate.Metadata.CatalogRevision = 1;

	const TSharedPtr<FJsonObject>* Metadata = nullptr;
	if (
		!Root->TryGetObjectField(TEXT("metadata"), Metadata) ||
		!Metadata ||
		!Metadata->IsValid()
	)
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_METADATA_MISSING");
		return false;
	}
	static const TSet<FString> MetadataFields = {
		TEXT("visual_description"),
		TEXT("intent_tags"),
		TEXT("emotion_tags"),
		TEXT("personality_tags"),
		TEXT("body_region_tags"),
		TEXT("spatial_requirement_tags"),
		TEXT("semantic_effect_tags"),
		TEXT("target_category_tags"),
		TEXT("required_capabilities"),
		TEXT("required_channels"),
		TEXT("blocked_states"),
		TEXT("requires_target"),
		TEXT("can_run_while_moving"),
		TEXT("allow_runtime_model_selection"),
		TEXT("cooldown_seconds"),
		TEXT("expressiveness"),
		TEXT("energy"),
		TEXT("social_intensity"),
		TEXT("variant_difference")
	};
	if (!ValidateFields(
		*Metadata,
		MetadataFields,
		TEXT("LLMNPC_ANIMATION_DRAFT_METADATA"),
		OutError
	))
	{
		return false;
	}
	if (
		!GetString(
			*Metadata,
			TEXT("visual_description"),
			OutTemplate.Metadata.VisualDescription,
			OutError
		) ||
		OutTemplate.Metadata.VisualDescription.Len() > 600 ||
		!GetNameArray(
			*Metadata,
			TEXT("intent_tags"),
			OutTemplate.Metadata.IntentTags,
			OutError
		) ||
		!GetNameArray(
			*Metadata,
			TEXT("emotion_tags"),
			OutTemplate.Metadata.EmotionTags,
			OutError
		) ||
		!GetNameArray(
			*Metadata,
			TEXT("personality_tags"),
			OutTemplate.Metadata.PersonalityTags,
			OutError
		) ||
		!GetNameArray(
			*Metadata,
			TEXT("body_region_tags"),
			OutTemplate.Metadata.BodyRegionTags,
			OutError
		) ||
		!GetNameArray(
			*Metadata,
			TEXT("spatial_requirement_tags"),
			OutTemplate.Metadata.SpatialRequirementTags,
			OutError
		) ||
		!GetNameArray(
			*Metadata,
			TEXT("semantic_effect_tags"),
			OutTemplate.Metadata.SemanticEffectTags,
			OutError
		) ||
		!GetNameArray(
			*Metadata,
			TEXT("target_category_tags"),
			OutTemplate.Metadata.TargetCategoryTags,
			OutError
		) ||
		!GetNameArray(
			*Metadata,
			TEXT("required_capabilities"),
			OutTemplate.Metadata.RequiredCapabilities,
			OutError
		) ||
		!GetNameArray(
			*Metadata,
			TEXT("required_channels"),
			OutTemplate.Metadata.RequiredChannels,
			OutError
		) ||
		!GetNameArray(
			*Metadata,
			TEXT("blocked_states"),
			OutTemplate.Metadata.BlockedStates,
			OutError
		) ||
		!GetBool(
			*Metadata,
			TEXT("requires_target"),
			OutTemplate.Metadata.bRequiresTarget,
			OutError
		) ||
		!GetBool(
			*Metadata,
			TEXT("can_run_while_moving"),
			OutTemplate.Metadata.bCanRunWhileMoving,
			OutError
		) ||
		!GetBool(
			*Metadata,
			TEXT("allow_runtime_model_selection"),
			OutTemplate.Metadata.bAllowRuntimeModelSelection,
			OutError
		) ||
		!GetNumber(
			*Metadata,
			TEXT("cooldown_seconds"),
			OutTemplate.Metadata.CooldownSeconds,
			OutError
		) ||
		!GetNumber(
			*Metadata,
			TEXT("expressiveness"),
			OutTemplate.Metadata.Expressiveness,
			OutError
		) ||
		!GetNumber(
			*Metadata,
			TEXT("energy"),
			OutTemplate.Metadata.Energy,
			OutError
		) ||
		!GetNumber(
			*Metadata,
			TEXT("social_intensity"),
			OutTemplate.Metadata.SocialIntensity,
			OutError
		)
	)
	{
		return false;
	}
	(*Metadata)->TryGetStringField(
		TEXT("variant_difference"),
		OutTemplate.Metadata.VariantDifference
	);
	OutTemplate.Metadata.VariantDifference =
		OutTemplate.Metadata.VariantDifference.TrimStartAndEnd();
	if (
		OutTemplate.Metadata.BodyRegionTags.IsEmpty() ||
		OutTemplate.Metadata.SemanticEffectTags.IsEmpty() ||
		OutTemplate.Metadata.RequiredCapabilities.IsEmpty() ||
		OutTemplate.Metadata.RequiredChannels.IsEmpty() ||
		OutTemplate.Metadata.CooldownSeconds < 0.0f ||
		OutTemplate.Metadata.CooldownSeconds > 60.0f ||
		OutTemplate.Metadata.Expressiveness < 0.0f ||
		OutTemplate.Metadata.Expressiveness > 1.0f ||
		OutTemplate.Metadata.Energy < 0.0f ||
		OutTemplate.Metadata.Energy > 1.0f ||
		OutTemplate.Metadata.SocialIntensity < 0.0f ||
		OutTemplate.Metadata.SocialIntensity > 1.0f ||
		OutTemplate.Metadata.bRequiresTarget !=
			!OutTemplate.Metadata.TargetCategoryTags.IsEmpty()
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_METADATA_CONTRACT_INVALID");
		return false;
	}

	const TSharedPtr<FJsonObject>* ModifierPolicy = nullptr;
	if (
		!Root->TryGetObjectField(
			TEXT("modifier_policy"),
			ModifierPolicy
		) ||
		!ModifierPolicy ||
		!ModifierPolicy->IsValid()
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_MODIFIER_POLICY_MISSING");
		return false;
	}
	static const TSet<FString> ModifierFields = {
		TEXT("amplitude"),
		TEXT("speed"),
		TEXT("duration"),
		TEXT("allow_mirror"),
		TEXT("allowed_styles"),
		TEXT("random_speed_jitter")
	};
	if (!ValidateFields(
		*ModifierPolicy,
		ModifierFields,
		TEXT("LLMNPC_ANIMATION_DRAFT_MODIFIER"),
		OutError
	))
	{
		return false;
	}
	if (
		!GetRange(
			*ModifierPolicy,
			TEXT("amplitude"),
			OutTemplate.ModifierPolicy.AmplitudeRange,
			OutError
		) ||
		!GetRange(
			*ModifierPolicy,
			TEXT("speed"),
			OutTemplate.ModifierPolicy.SpeedRange,
			OutError
		) ||
		!GetRange(
			*ModifierPolicy,
			TEXT("duration"),
			OutTemplate.ModifierPolicy.DurationRange,
			OutError
		) ||
		!GetBool(
			*ModifierPolicy,
			TEXT("allow_mirror"),
			OutTemplate.ModifierPolicy.bAllowMirror,
			OutError
		) ||
		!GetNameArray(
			*ModifierPolicy,
			TEXT("allowed_styles"),
			OutTemplate.ModifierPolicy.AllowedStyleTags,
			OutError
		) ||
		!GetOptionalNumber(
			*ModifierPolicy,
			TEXT("random_speed_jitter"),
			0.0f,
			OutTemplate.ModifierPolicy.RandomSpeedJitter,
			OutError
		)
	)
	{
		return false;
	}
	if (
		!OutTemplate.ModifierPolicy.AmplitudeRange.Equals(
			FVector2D(1.0f, 1.0f),
			KINDA_SMALL_NUMBER
		) ||
		OutTemplate.ModifierPolicy.bAllowMirror ||
		OutTemplate.ModifierPolicy.RandomSpeedJitter < 0.0f ||
		OutTemplate.ModifierPolicy.RandomSpeedJitter > 0.25f ||
		OutTemplate.ModifierPolicy.AllowedStyleTags.IsEmpty()
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_ASSET_MODIFIER_POLICY_INVALID");
		return false;
	}
	for (const FName Style : OutTemplate.Metadata.VariantStyleTags)
	{
		if (!OutTemplate.ModifierPolicy.AllowedStyleTags.Contains(Style))
		{
			OutError =
				TEXT("LLMNPC_ANIMATION_DRAFT_VARIANT_STYLE_FORBIDDEN");
			return false;
		}
	}
	OutTemplate.ModifierPolicy.PolicyVersion = 2;
	OutTemplate.ModifierPolicy.ReachScaleRange =
		FVector2D(1.0f, 1.0f);
	OutTemplate.ModifierPolicy.HeightScaleRange =
		FVector2D(1.0f, 1.0f);
	OutTemplate.ModifierPolicy.LateralScaleRange =
		FVector2D(1.0f, 1.0f);
	OutTemplate.ModifierPolicy.CycleCountRange = FIntPoint::ZeroValue;
	OutTemplate.ModifierPolicy.GazeEngagementRange =
		FVector2D(1.0f, 1.0f);
	OutTemplate.ModifierPolicy.PalmOrientationWeightRange =
		FVector2D(1.0f, 1.0f);
	OutTemplate.ModifierPolicy.FingerPoseWeightRange =
		FVector2D(1.0f, 1.0f);
	OutTemplate.ModifierPolicy.TorsoParticipationRange =
		FVector2D(1.0f, 1.0f);
	OutTemplate.ModifierPolicy.BlendInScaleRange =
		FVector2D(1.0f, 1.0f);
	OutTemplate.ModifierPolicy.BlendOutScaleRange =
		FVector2D(1.0f, 1.0f);
	OutTemplate.ModifierPolicy.bEnableDynamicTargetTracking = false;
	OutTemplate.ModifierPolicy.bEnableObstacleAdaptation = false;
	OutTemplate.ModifierPolicy.RandomAmplitudeJitter = 0.0f;
	OutTemplate.ModifierPolicy.RandomFrequencyJitter = 0.0f;
	OutTemplate.ModifierPolicy.RandomPhaseJitterRadians = 0.0f;

	const TSharedPtr<FJsonObject>* Playback = nullptr;
	if (
		!Root->TryGetObjectField(TEXT("playback"), Playback) ||
		!Playback ||
		!Playback->IsValid()
	)
	{
		OutError = TEXT("LLMNPC_ANIMATION_DRAFT_PLAYBACK_MISSING");
		return false;
	}
	static const TSet<FString> PlaybackFields = {
		TEXT("slot_name"),
		TEXT("blend_in_seconds"),
		TEXT("blend_out_seconds"),
		TEXT("start_position_seconds"),
		TEXT("max_duration_seconds"),
		TEXT("loop"),
		TEXT("interruptible"),
		TEXT("stop_other_montages"),
		TEXT("allow_root_motion")
	};
	if (!ValidateFields(
		*Playback,
		PlaybackFields,
		TEXT("LLMNPC_ANIMATION_DRAFT_PLAYBACK"),
		OutError
	))
	{
		return false;
	}
	if (
		!GetString(*Playback, TEXT("slot_name"), Value, OutError) ||
		!IsSafeIdentifier(Value, 128)
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_SLOT_NAME_INVALID");
		return false;
	}
	OutTemplate.AnimationPlayback.SlotName = FName(*Value);
	if (
		!GetNumber(
			*Playback,
			TEXT("blend_in_seconds"),
			OutTemplate.AnimationPlayback.BlendInSeconds,
			OutError
		) ||
		!GetNumber(
			*Playback,
			TEXT("blend_out_seconds"),
			OutTemplate.AnimationPlayback.BlendOutSeconds,
			OutError
		) ||
		!GetNumber(
			*Playback,
			TEXT("start_position_seconds"),
			OutTemplate.AnimationPlayback.StartPositionSeconds,
			OutError
		) ||
		!GetNumber(
			*Playback,
			TEXT("max_duration_seconds"),
			OutTemplate.AnimationPlayback.MaxDurationSeconds,
			OutError
		) ||
		!GetBool(
			*Playback,
			TEXT("loop"),
			OutTemplate.AnimationPlayback.bLoop,
			OutError
		) ||
		!GetBool(
			*Playback,
			TEXT("interruptible"),
			OutTemplate.AnimationPlayback.bInterruptible,
			OutError
		) ||
		!GetBool(
			*Playback,
			TEXT("stop_other_montages"),
			OutTemplate.AnimationPlayback.bStopOtherMontages,
			OutError
		) ||
		!GetBool(
			*Playback,
			TEXT("allow_root_motion"),
			OutTemplate.AnimationPlayback.bAllowRootMotion,
			OutError
		)
	)
	{
		return false;
	}
	if (
		OutTemplate.AnimationPlayback.BlendInSeconds < 0.0f ||
		OutTemplate.AnimationPlayback.BlendInSeconds > 2.0f ||
		OutTemplate.AnimationPlayback.BlendOutSeconds < 0.0f ||
		OutTemplate.AnimationPlayback.BlendOutSeconds > 2.0f ||
		OutTemplate.AnimationPlayback.StartPositionSeconds < 0.0f ||
		OutTemplate.AnimationPlayback.StartPositionSeconds >=
			Sequence->GetPlayLength() ||
		OutTemplate.AnimationPlayback.MaxDurationSeconds < 0.1f ||
		OutTemplate.AnimationPlayback.MaxDurationSeconds > 60.0f ||
		OutTemplate.AnimationPlayback.bAllowRootMotion
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_PLAYBACK_POLICY_INVALID");
		return false;
	}
	if (
		!SelectedAnimationAsset.GetSkeleton()->ContainsSlotName(
			OutTemplate.AnimationPlayback.SlotName
		)
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_SLOT_NOT_REGISTERED");
		return false;
	}

	const TSharedPtr<FJsonObject>* Provenance = nullptr;
	if (
		!Root->TryGetObjectField(TEXT("provenance"), Provenance) ||
		!Provenance ||
		!Provenance->IsValid()
	)
	{
		OutError =
			TEXT("LLMNPC_ANIMATION_DRAFT_PROVENANCE_MISSING");
		return false;
	}
	if (!ParseProvenance(
		*Provenance,
		SelectedAnimationAsset,
		OutInfo,
		OutTemplate.SourceProvenanceJson,
		OutError
	))
	{
		return false;
	}

	OutTemplate.AnimationAsset =
		const_cast<UAnimationAsset*>(&SelectedAnimationAsset);
	OutTemplate.Metadata.SourceRecipeHash =
		FString::Printf(
			TEXT("animation_asset:%s:%s"),
			*OutInfo.SelectedAnimationAssetPath,
			*OutInfo.SelectedAnimationAssetPackageHash
		);
	FString TemplateError;
	if (!OutTemplate.ValidateTemplate(TemplateError))
	{
		OutError = TemplateError;
		return false;
	}
	return true;
}
