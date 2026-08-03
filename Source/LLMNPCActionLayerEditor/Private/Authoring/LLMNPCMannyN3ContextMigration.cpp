#include "Authoring/LLMNPCMannyN3ContextMigration.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Context/LLMNPCModifierMappingProfile.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Templates/LLMNPCActionVocabulary.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"
#include "Templates/LLMNPCTemplateSearchIndex.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMNPCMannyN3Migration, Log, All);

namespace
{
const TCHAR* N3MappingPackagePath =
	TEXT("/LLMNPCActionLayer/LLMNPC/Context/MP_Manny_Default_v1");
const TCHAR* N3MappingAssetName = TEXT("MP_Manny_Default_v1");

struct FN3TemplateSeed
{
	const TCHAR* AssetPath;
	const TCHAR* SourceFilename;
	bool bOptional = false;
};

TArray<FN3TemplateSeed> N3GetTemplateSeeds()
{
	return {
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Nod_Manny_v1.MT_Nod_Manny_v1"),
			TEXT("gesture.nod.manny.v1.json")
		},
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Point_Target_Manny_v1.MT_Point_Target_Manny_v1"),
			TEXT("gesture.point.target.manny.v1.json")
		},
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_FK_v1.MT_Wave_Right_Manny_FK_v1"),
			TEXT("gesture.wave.right.manny.fk.v1.json")
		},
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_Procedural_v1.MT_Wave_Right_Manny_Procedural_v1"),
			TEXT("gesture.wave.right.manny.procedural.v1.json")
		},
		{
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Wave_Right_Manny_Subtle_v1.MT_Wave_Right_Manny_Subtle_v1"),
			TEXT("gesture.wave.right.manny.subtle.v1.json")
		},
		{
			TEXT("/Game/LLMNPCActionLayer/MotionTemplates/MT_Wave_Asset_Manny_v1.MT_Wave_Asset_Manny_v1"),
			TEXT(""),
			true
		}
	};
}

template<typename T>
T* N3LoadOrCreateAsset(
	const FString& PackagePath,
	const FString& AssetName,
	FString& OutError
)
{
	const FString ObjectPath = PackagePath + TEXT(".") + AssetName;
	if (T* Existing = LoadObject<T>(nullptr, *ObjectPath))
	{
		Existing->Modify();
		return Existing;
	}
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutError = TEXT("LLMNPC_N3_PACKAGE_CREATE_FAILED");
		return nullptr;
	}
	T* Asset = NewObject<T>(
		Package,
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional
	);
	if (!Asset)
	{
		OutError = TEXT("LLMNPC_N3_ASSET_CREATE_FAILED");
		return nullptr;
	}
	FAssetRegistryModule::AssetCreated(Asset);
	Asset->Modify();
	return Asset;
}

bool N3SaveAsset(UObject* Asset, FString& OutError)
{
	if (!Asset)
	{
		OutError = TEXT("LLMNPC_N3_ASSET_MISSING");
		return false;
	}
	UPackage* Package = Asset->GetOutermost();
	Package->MarkPackageDirty();
	const FString Filename = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(Package, Asset, *Filename, SaveArgs))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_N3_ASSET_SAVE_FAILED:%s"),
			*Package->GetName()
		);
		return false;
	}
	return true;
}

void N3ResetContextPolicy(FLLMNPCModifierPolicy& Policy)
{
	Policy.PolicyVersion = 2;
	Policy.ReachScaleRange = FVector2D(1.0f, 1.0f);
	Policy.HeightScaleRange = FVector2D(1.0f, 1.0f);
	Policy.LateralScaleRange = FVector2D(1.0f, 1.0f);
	Policy.CycleCountRange = FIntPoint::ZeroValue;
	Policy.GazeEngagementRange = FVector2D(1.0f, 1.0f);
	Policy.PalmOrientationWeightRange = FVector2D(1.0f, 1.0f);
	Policy.FingerPoseWeightRange = FVector2D(1.0f, 1.0f);
	Policy.TorsoParticipationRange = FVector2D(1.0f, 1.0f);
	Policy.BlendInScaleRange = FVector2D(0.85f, 1.15f);
	Policy.BlendOutScaleRange = FVector2D(0.85f, 1.15f);
	Policy.bEnableDynamicTargetTracking = false;
	Policy.MaxTargetFollowSpeedCmPerSecond = 240.0f;
	Policy.MaxTargetAngularSpeedDegreesPerSecond = 180.0f;
	Policy.TargetInterpolationSpeed = 8.0f;
	Policy.TargetTeleportThresholdCm = 250.0f;
	Policy.TargetLostFadeSeconds = 0.18f;
	Policy.TargetLossPolicy = ELLMNPCTargetLossPolicy::FadeOut;
	Policy.bEnableObstacleAdaptation = false;
	Policy.MinObstacleAmplitudeScale = 0.6f;
	Policy.MinObstacleReachScale = 0.65f;
	Policy.ObstacleCancelClearance = 0.2f;
}

void N3ConfigureTemplate(ULLMNPCMotionTemplate& Template)
{
	FLLMNPCModifierPolicy& Policy = Template.ModifierPolicy;
	N3ResetContextPolicy(Policy);
	const FName TemplateId = Template.Metadata.TemplateId;
	if (TemplateId == TEXT("gesture.point.target.manny.v1"))
	{
		Policy.bAllowMirror = true;
		Policy.ReachScaleRange = FVector2D(0.70f, 1.08f);
		Policy.HeightScaleRange = FVector2D(0.55f, 1.0f);
		Policy.LateralScaleRange = FVector2D(0.75f, 1.0f);
		Policy.GazeEngagementRange = FVector2D(0.2f, 1.0f);
		Policy.PalmOrientationWeightRange = FVector2D(0.7f, 1.0f);
		Policy.FingerPoseWeightRange = FVector2D(0.75f, 1.0f);
		Policy.TorsoParticipationRange = FVector2D(0.8f, 1.0f);
		Policy.BlendInScaleRange = FVector2D(0.85f, 1.2f);
		Policy.BlendOutScaleRange = FVector2D(0.85f, 1.2f);
		Policy.bEnableDynamicTargetTracking = true;
		Policy.MaxTargetFollowSpeedCmPerSecond = 260.0f;
		Policy.MaxTargetAngularSpeedDegreesPerSecond = 160.0f;
		Policy.TargetInterpolationSpeed = 8.0f;
		Policy.TargetTeleportThresholdCm = 250.0f;
		Policy.TargetLostFadeSeconds = 0.2f;
		Policy.bEnableObstacleAdaptation = true;
	}
	else if (TemplateId.ToString().Contains(TEXT("gesture.wave.right.manny")))
	{
		Policy.bAllowMirror =
			TemplateId != TEXT("gesture.wave.right.manny.procedural.v1");
		Policy.ReachScaleRange = FVector2D(0.75f, 1.0f);
		Policy.HeightScaleRange = FVector2D(0.8f, 1.05f);
		Policy.LateralScaleRange = FVector2D(0.7f, 1.0f);
		Policy.PalmOrientationWeightRange = FVector2D(0.75f, 1.0f);
		Policy.FingerPoseWeightRange = FVector2D(0.7f, 1.0f);
		Policy.TorsoParticipationRange = FVector2D(0.8f, 1.0f);
		Policy.bEnableObstacleAdaptation = true;
	}
	if (TemplateId == TEXT("gesture.wave.right.manny.procedural.v1"))
	{
		Template.Metadata.CatalogRevision = 3;
		Template.Metadata.SemanticVersion = TEXT("1.1.1");
		Template.Metadata.VariantDifference =
			TEXT("Strict right-hand procedural comparison variant; mirroring disabled after N8 visual rejection.");
	}
	else
	{
		Template.Metadata.CatalogRevision = 2;
		Template.Metadata.SemanticVersion =
			Template.Kind == ELLMNPCTemplateKind::AnimationAsset
				? TEXT("1.0.1")
				: TEXT("1.1.0");
	}
	Template.Metadata.CatalogContentHash.Reset();
	Template.Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(Template);
}

TArray<TSharedPtr<FJsonValue>> N3RangeToJson(const FVector2D& Range)
{
	return {
		MakeShared<FJsonValueNumber>(Range.X),
		MakeShared<FJsonValueNumber>(Range.Y)
	};
}

TArray<TSharedPtr<FJsonValue>> N3IntRangeToJson(const FIntPoint& Range)
{
	return {
		MakeShared<FJsonValueNumber>(Range.X),
		MakeShared<FJsonValueNumber>(Range.Y)
	};
}

FString N3TargetLossPolicyToString(ELLMNPCTargetLossPolicy Policy)
{
	switch (Policy)
	{
	case ELLMNPCTargetLossPolicy::CancelMotion:
		return TEXT("cancel");
	case ELLMNPCTargetLossPolicy::HoldLast:
		return TEXT("hold_last");
	case ELLMNPCTargetLossPolicy::FadeOut:
	default:
		return TEXT("fade_out");
	}
}

bool N3WriteJson(
	const FString& Path,
	const TSharedRef<FJsonObject>& Root,
	FString& OutError
)
{
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&Json);
	if (
		!FJsonSerializer::Serialize(Root, Writer) ||
		!FFileHelper::SaveStringToFile(
			Json,
			*Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
		)
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_N3_RESOURCE_WRITE_FAILED:%s"),
			*Path
		);
		return false;
	}
	return true;
}

bool N3UpdateTemplateSource(
	const FString& Path,
	const ULLMNPCMotionTemplate& Template,
	FString& OutError
)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_N3_SOURCE_READ_FAILED:%s"),
			*Path
		);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_N3_SOURCE_JSON_INVALID");
		return false;
	}
	const TSharedPtr<FJsonObject>* ExistingPolicy = nullptr;
	if (
		!Root->TryGetObjectField(TEXT("modifier_policy"), ExistingPolicy) ||
		!ExistingPolicy ||
		!ExistingPolicy->IsValid()
	)
	{
		OutError = TEXT("LLMNPC_N3_SOURCE_POLICY_MISSING");
		return false;
	}
	const TSharedRef<FJsonObject> Policy =
		MakeShared<FJsonObject>(**ExistingPolicy);
	const FLLMNPCModifierPolicy& Value = Template.ModifierPolicy;
	Policy->SetNumberField(TEXT("policy_version"), Value.PolicyVersion);
	Policy->SetBoolField(TEXT("allow_mirror"), Value.bAllowMirror);
	Policy->SetArrayField(TEXT("reach_scale"), N3RangeToJson(Value.ReachScaleRange));
	Policy->SetArrayField(TEXT("height_scale"), N3RangeToJson(Value.HeightScaleRange));
	Policy->SetArrayField(TEXT("lateral_scale"), N3RangeToJson(Value.LateralScaleRange));
	Policy->SetArrayField(TEXT("cycle_count"), N3IntRangeToJson(Value.CycleCountRange));
	Policy->SetArrayField(TEXT("gaze_engagement"), N3RangeToJson(Value.GazeEngagementRange));
	Policy->SetArrayField(TEXT("palm_orientation_weight"), N3RangeToJson(Value.PalmOrientationWeightRange));
	Policy->SetArrayField(TEXT("finger_pose_weight"), N3RangeToJson(Value.FingerPoseWeightRange));
	Policy->SetArrayField(TEXT("torso_participation"), N3RangeToJson(Value.TorsoParticipationRange));
	Policy->SetArrayField(TEXT("blend_in_scale"), N3RangeToJson(Value.BlendInScaleRange));
	Policy->SetArrayField(TEXT("blend_out_scale"), N3RangeToJson(Value.BlendOutScaleRange));
	Policy->SetBoolField(TEXT("enable_dynamic_target_tracking"), Value.bEnableDynamicTargetTracking);
	Policy->SetNumberField(TEXT("max_target_follow_speed_cm_per_second"), Value.MaxTargetFollowSpeedCmPerSecond);
	Policy->SetNumberField(TEXT("max_target_angular_speed_degrees_per_second"), Value.MaxTargetAngularSpeedDegreesPerSecond);
	Policy->SetNumberField(TEXT("target_interpolation_speed"), Value.TargetInterpolationSpeed);
	Policy->SetNumberField(TEXT("target_teleport_threshold_cm"), Value.TargetTeleportThresholdCm);
	Policy->SetNumberField(TEXT("target_lost_fade_seconds"), Value.TargetLostFadeSeconds);
	Policy->SetStringField(TEXT("target_loss_policy"), N3TargetLossPolicyToString(Value.TargetLossPolicy));
	Policy->SetBoolField(TEXT("enable_obstacle_adaptation"), Value.bEnableObstacleAdaptation);
	Policy->SetNumberField(TEXT("min_obstacle_amplitude_scale"), Value.MinObstacleAmplitudeScale);
	Policy->SetNumberField(TEXT("min_obstacle_reach_scale"), Value.MinObstacleReachScale);
	Policy->SetNumberField(TEXT("obstacle_cancel_clearance"), Value.ObstacleCancelClearance);
	Root->SetObjectField(TEXT("modifier_policy"), Policy);
	Root->SetStringField(TEXT("semantic_version"), Template.Metadata.SemanticVersion);
	Root->SetNumberField(TEXT("catalog_revision"), Template.Metadata.CatalogRevision);
	Root->SetStringField(TEXT("variant_difference"), Template.Metadata.VariantDifference);
	Root->SetStringField(TEXT("catalog_content_hash"), Template.Metadata.CatalogContentHash);
	return N3WriteJson(Path, Root.ToSharedRef(), OutError);
}

bool N3UpdateCatalogArtifact(
	const FString& Path,
	const FString& CatalogHash,
	FString& OutError
)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = TEXT("LLMNPC_N3_CATALOG_ARTIFACT_READ_FAILED");
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_N3_CATALOG_ARTIFACT_JSON_INVALID");
		return false;
	}
	Root->SetStringField(TEXT("catalog_hash"), CatalogHash);
	return N3WriteJson(Path, Root.ToSharedRef(), OutError);
}
}

bool FLLMNPCMannyN3ContextMigration::Run(
	FString& OutCatalogHash,
	FString& OutError
)
{
	OutCatalogHash.Reset();
	OutError.Reset();
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	if (!Plugin.IsValid())
	{
		OutError = TEXT("LLMNPC_N3_PLUGIN_NOT_FOUND");
		return false;
	}

	ULLMNPCModifierMappingProfile* Mapping =
		N3LoadOrCreateAsset<ULLMNPCModifierMappingProfile>(
			N3MappingPackagePath,
			N3MappingAssetName,
			OutError
		);
	if (!Mapping)
	{
		return false;
	}
	Mapping->SchemaVersion = TEXT("llmnpc.modifier_mapping_profile.v1");
	Mapping->ProfileId = TEXT("manny.default.v1");
	Mapping->Rules = ULLMNPCModifierMappingProfile::BuildMannyDefaultRules();
	if (!Mapping->Validate(OutError) || !N3SaveAsset(Mapping, OutError))
	{
		return false;
	}

	TArray<ULLMNPCMotionTemplate*> Templates;
	const FString SourceDirectory = FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("Templates"),
		TEXT("Manny")
	);
	for (const FN3TemplateSeed& Seed : N3GetTemplateSeeds())
	{
		ULLMNPCMotionTemplate* Template =
			LoadObject<ULLMNPCMotionTemplate>(nullptr, Seed.AssetPath);
		if (!Template)
		{
			if (Seed.bOptional)
			{
				continue;
			}
			OutError = FString::Printf(
				TEXT("LLMNPC_N3_TEMPLATE_LOAD_FAILED:%s"),
				Seed.AssetPath
			);
			return false;
		}
		Template->Modify();
		N3ConfigureTemplate(*Template);
		if (
			!Template->ValidateTemplate(OutError) ||
			!N3SaveAsset(Template, OutError)
		)
		{
			return false;
		}
		if (
			FString(Seed.SourceFilename).Len() > 0 &&
			!N3UpdateTemplateSource(
				FPaths::Combine(SourceDirectory, Seed.SourceFilename),
				*Template,
				OutError
			)
		)
		{
			return false;
		}
		Templates.Add(Template);
	}

	ULLMNPCActionVocabulary* Vocabulary =
		LoadObject<ULLMNPCActionVocabulary>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/Catalog/AV_LLMNPC_Default.AV_LLMNPC_Default")
		);
	if (!Vocabulary)
	{
		OutError = TEXT("LLMNPC_N3_VOCABULARY_LOAD_FAILED");
		return false;
	}
	TArray<ULLMNPCPublicActionDefinition*> Definitions;
	for (const TCHAR* Path : {
		TEXT("/LLMNPCActionLayer/LLMNPC/PublicActions/PA_Gesture_Nod.PA_Gesture_Nod"),
		TEXT("/LLMNPCActionLayer/LLMNPC/PublicActions/PA_Gesture_Point_Target.PA_Gesture_Point_Target"),
		TEXT("/LLMNPCActionLayer/LLMNPC/PublicActions/PA_Gesture_Wave_Right.PA_Gesture_Wave_Right")})
	{
		ULLMNPCPublicActionDefinition* Definition =
			LoadObject<ULLMNPCPublicActionDefinition>(nullptr, Path);
		if (!Definition)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_N3_PUBLIC_ACTION_LOAD_FAILED:%s"),
				Path
			);
			return false;
		}
		Definitions.Add(Definition);
	}
	if (ULLMNPCPublicActionDefinition* Clap =
		LoadObject<ULLMNPCPublicActionDefinition>(
			nullptr,
			TEXT(
				"/LLMNPCActionLayer/LLMNPC/PublicActions/"
				"PA_Gesture_Clap.PA_Gesture_Clap"
			)
		))
	{
		Definitions.Add(Clap);
	}

	FLLMNPCTemplateSearchIndex Index;
	if (
		!Index.Build(
			Templates,
			Definitions,
			Vocabulary,
			{ TEXT("ue5_manny.v1") }
		) ||
		!Index.GetDiagnostics().IsEmpty()
	)
	{
		OutError = Index.GetDiagnostics().IsEmpty()
			? TEXT("LLMNPC_N3_CATALOG_BUILD_FAILED")
			: Index.GetDiagnostics()[0].Code.ToString();
		return false;
	}
	OutCatalogHash = Index.GetCatalogHash();
	if (!N3UpdateCatalogArtifact(
		FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources"),
			TEXT("Catalog"),
			TEXT("Manny"),
			TEXT("public_actions_v1.json")
		),
		OutCatalogHash,
		OutError
	))
	{
		return false;
	}
	UE_LOG(
		LogLLMNPCMannyN3Migration,
		Display,
		TEXT("Manny N3 Context migration completed. CatalogHash=%s"),
		*OutCatalogHash
	);
	return true;
}
