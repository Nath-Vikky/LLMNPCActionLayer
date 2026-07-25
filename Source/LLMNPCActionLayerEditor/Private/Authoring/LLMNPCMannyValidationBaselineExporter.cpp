#include "Authoring/LLMNPCMannyValidationBaselineExporter.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Capabilities/LLMNPCSkeletonCapabilityTypes.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "LLMNPCControlManifest.h"
#include "Quality/LLMNPCKinematicValidator.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "UObject/UObjectGlobals.h"

namespace
{
FString HashFileMd5(const FString& Filename)
{
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *Filename))
	{
		return FString();
	}
	return FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashBytes(Data.GetData(), Data.Num())
	);
}

TSharedRef<FJsonObject> BuildPoseEvidence(
	const FString& AssetPath,
	const FString& Role
)
{
	const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
	const FString Filename = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension()
	);
	TSharedRef<FJsonObject> Evidence = MakeShared<FJsonObject>();
	Evidence->SetStringField(TEXT("asset_path"), AssetPath);
	Evidence->SetStringField(TEXT("role"), Role);
	Evidence->SetStringField(TEXT("asset_hash"), HashFileMd5(Filename));
	return Evidence;
}

TSharedRef<FJsonObject> BuildTrackMetricsJson(
	const FLLMNPCKinematicTrackMetrics& Metrics
)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("control_id"), Metrics.ControlId.ToString());
	Object->SetNumberField(TEXT("max_absolute_value"), Metrics.MaxAbsoluteValue);
	Object->SetNumberField(TEXT("max_absolute_speed"), Metrics.MaxAbsoluteSpeed);
	Object->SetNumberField(
		TEXT("max_absolute_acceleration"),
		Metrics.MaxAbsoluteAcceleration
	);
	Object->SetNumberField(TEXT("max_absolute_jerk"), Metrics.MaxAbsoluteJerk);
	Object->SetNumberField(TEXT("start_absolute_value"), Metrics.StartAbsoluteValue);
	Object->SetNumberField(TEXT("end_absolute_value"), Metrics.EndAbsoluteValue);
	return Object;
}

TSharedRef<FJsonObject> BuildIssueJson(
	const FLLMNPCKinematicValidationIssue& Issue
)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("code"), Issue.Code);
	Object->SetNumberField(TEXT("severity"), static_cast<int32>(Issue.Severity));
	Object->SetStringField(TEXT("field_path"), Issue.FieldPath);
	Object->SetNumberField(TEXT("sample_time_seconds"), Issue.SampleTimeSeconds);
	Object->SetNumberField(TEXT("observed_value"), Issue.ObservedValue);
	Object->SetNumberField(TEXT("limit_value"), Issue.LimitValue);
	return Object;
}

bool SerializeJson(
	const TSharedRef<FJsonObject>& Root,
	bool bPretty,
	FString& OutJson
)
{
	OutJson.Reset();
	if (bPretty)
	{
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJson);
		return FJsonSerializer::Serialize(Root, Writer);
	}
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	return FJsonSerializer::Serialize(Root, Writer);
}

struct FPublishedTemplateValidation
{
	FAssetData AssetData;
	const ULLMNPCMotionTemplate* MotionTemplate = nullptr;
	FLLMNPCTemplateModifiers Modifiers;
	FLLMNPCKinematicQualityReport Report;
};

struct FExistingBaselineMetadata
{
	FString BaselineHash;
	FString GeneratedAt;
	FString ApprovedBy;
	FString ApprovedAt;
	FString ApprovalNote;
	bool bApproved = false;
};

FName NormalizeConstraintControlId(FName ControlId)
{
	const FString Value = ControlId.ToString();
	return Value.StartsWith(TEXT("mirror_left_"))
		? FName(*Value.RightChop(7))
		: ControlId;
}

bool GatherPublishedTemplateValidations(
	const ULLMNPCSkeletonProfile& Profile,
	const FString& CapabilityHash,
	TArray<FPublishedTemplateValidation>& OutValidations,
	FString& OutError
)
{
	OutValidations.Reset();
	ULLMNPCSkeletonProfile* ValidationProfile =
		DuplicateObject<ULLMNPCSkeletonProfile>(
			&Profile,
			GetTransientPackage()
		);
	if (!ValidationProfile)
	{
		OutError = TEXT("LLMNPC_MANNY_BASELINE_PROFILE_DUPLICATE_FAILED");
		return false;
	}
	ValidationProfile->UpperBodyConstraints.bKinematicBaselineApproved = true;

	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	FARFilter Filter;
	Filter.ClassPaths.Add(ULLMNPCMotionTemplate::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny"));
	Filter.bRecursivePaths = true;
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	Assets.Sort(
		[](const FAssetData& A, const FAssetData& B)
		{
			return A.PackageName.LexicalLess(B.PackageName);
		}
	);

	for (const FAssetData& AssetData : Assets)
	{
		const ULLMNPCMotionTemplate* MotionTemplate =
			Cast<ULLMNPCMotionTemplate>(AssetData.GetAsset());
		if (
			!MotionTemplate ||
			!MotionTemplate->IsPublished() ||
			MotionTemplate->Kind != ELLMNPCTemplateKind::ProceduralMotion ||
			!MotionTemplate->SupportsSkeletonProfile(Profile.ProfileId)
		)
		{
			continue;
		}

		FLLMNPCTemplateModifiers Modifiers;
		if (MotionTemplate->Metadata.bRequiresTarget)
		{
			Modifiers.TargetRef = TEXT("validation_target");
		}
		if (
			!MotionTemplate->ModifierPolicy.AllowedStyleTags.IsEmpty() &&
			!MotionTemplate->ModifierPolicy.AllowedStyleTags.Contains(
				Modifiers.Style
			)
		)
		{
			TArray<FName> AllowedStyles =
				MotionTemplate->ModifierPolicy.AllowedStyleTags;
			AllowedStyles.Sort(FNameLexicalLess());
			Modifiers.Style = AllowedStyles[0];
		}

		FLLMMotionPlan Plan;
		FString CompileError;
		if (!FLLMNPCTemplateCompiler::Compile(
			*MotionTemplate,
			Modifiers,
			*ValidationProfile,
			Plan,
			CompileError
		))
		{
			OutError = FString::Printf(
				TEXT("%s:%s"),
				*MotionTemplate->Metadata.TemplateId.ToString(),
				*CompileError
			);
			return false;
		}

		FLLMNPCKinematicValidationSettings Settings;
		Settings.SampleRateHz = 60.0f;
		FPublishedTemplateValidation& Validation =
			OutValidations.AddDefaulted_GetRef();
		Validation.AssetData = AssetData;
		Validation.MotionTemplate = MotionTemplate;
		Validation.Modifiers = Modifiers;
		Validation.Report = FLLMNPCKinematicValidator::ValidatePlan(
			Plan,
			*ValidationProfile,
			nullptr,
			CapabilityHash,
			Settings
		);
	}

	if (OutValidations.IsEmpty())
	{
		OutError = TEXT("LLMNPC_MANNY_BASELINE_TEMPLATE_SET_EMPTY");
		return false;
	}
	return true;
}

float RoundObservedLimit(
	double ObservedValue,
	float HeadroomMultiplier,
	float Quantum
)
{
	if (
		!FMath::IsFinite(ObservedValue) ||
		ObservedValue <= 0.0 ||
		!FMath::IsFinite(HeadroomMultiplier) ||
		HeadroomMultiplier < 1.0f
	)
	{
		return 0.0f;
	}
	return Quantum * FMath::CeilToFloat(
		static_cast<float>(ObservedValue) * HeadroomMultiplier / Quantum
	);
}

void LoadExistingBaselineMetadata(
	const FString& Filename,
	FExistingBaselineMetadata& OutMetadata
)
{
	OutMetadata = FExistingBaselineMetadata();
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Filename))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	Root->TryGetStringField(TEXT("baseline_hash"), OutMetadata.BaselineHash);
	Root->TryGetStringField(TEXT("generated_at"), OutMetadata.GeneratedAt);
	FString Status;
	Root->TryGetStringField(TEXT("status"), Status);
	OutMetadata.bApproved = Status == TEXT("approved");
	const TSharedPtr<FJsonObject>* Approval = nullptr;
	if (
		Root->TryGetObjectField(TEXT("human_approval"), Approval) &&
		Approval &&
		Approval->IsValid()
	)
	{
		(*Approval)->TryGetStringField(
			TEXT("approved_by"),
			OutMetadata.ApprovedBy
		);
		(*Approval)->TryGetStringField(
			TEXT("approved_at"),
			OutMetadata.ApprovedAt
		);
		(*Approval)->TryGetStringField(
			TEXT("note"),
			OutMetadata.ApprovalNote
		);
	}
}
}

bool FLLMNPCMannyValidationBaselineExporter::Export(
	const ULLMNPCSkeletonProfile& Profile,
	const FLLMNPCSkeletonCapabilitySnapshot& Capability,
	const FString& OutputFilename,
	FString& OutError,
	FString* OutBaselineHash
)
{
	OutError.Reset();
	if (OutBaselineHash)
	{
		OutBaselineHash->Reset();
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.manny_validation_baseline.v1")
	);
	Root->SetStringField(TEXT("profile_id"), Profile.ProfileId.ToString());
	Root->SetStringField(
		TEXT("profile_semantic_version"),
		Profile.SemanticVersion
	);
	Root->SetStringField(TEXT("skeleton_signature"), Profile.SkeletonSignature);
	Root->SetStringField(TEXT("capability_hash"), Capability.CapabilityHash);
	Root->SetNumberField(TEXT("sample_rate_hz"), 60.0);

	TArray<TSharedPtr<FJsonValue>> PoseSet;
	PoseSet.Add(MakeShared<FJsonValueObject>(
		BuildPoseEvidence(
			TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Idle.MM_Idle"),
			TEXT("project_idle")
		)
	));
	PoseSet.Add(MakeShared<FJsonValueObject>(
		BuildPoseEvidence(
			TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Walk_InPlace.MM_Walk_InPlace"),
			TEXT("project_walk")
		)
	));
	PoseSet.Add(MakeShared<FJsonValueObject>(
		BuildPoseEvidence(
			TEXT("/Game/LLMNPC/Animation/Waving.Waving"),
			TEXT("social_motion_reference")
		)
	));
	Root->SetArrayField(TEXT("validation_pose_set"), PoseSet);

	FString PoseSetCanonical;
	for (const TSharedPtr<FJsonValue>& Pose : PoseSet)
	{
		const TSharedPtr<FJsonObject> Object = Pose->AsObject();
		PoseSetCanonical += Object->GetStringField(TEXT("asset_path"));
		PoseSetCanonical += TEXT("|");
		PoseSetCanonical += Object->GetStringField(TEXT("asset_hash"));
		PoseSetCanonical += TEXT("\n");
	}
	Root->SetStringField(
		TEXT("validation_pose_set_hash"),
		FString::Printf(TEXT("md5:%s"), *FMD5::HashAnsiString(*PoseSetCanonical))
	);

	TArray<FLLMNPCKinematicControlConstraint> Constraints =
		Profile.ControlConstraints;
	Constraints.Sort(
		[](const FLLMNPCKinematicControlConstraint& A, const FLLMNPCKinematicControlConstraint& B)
		{
			return A.ControlId.LexicalLess(B.ControlId);
		}
	);
	TArray<TSharedPtr<FJsonValue>> Thresholds;
	for (const FLLMNPCKinematicControlConstraint& Constraint : Constraints)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("control_id"), Constraint.ControlId.ToString());
		Object->SetNumberField(
			TEXT("max_angular_speed_degrees_per_second"),
			Constraint.MaxAngularSpeedDegreesPerSecond
		);
		Object->SetNumberField(
			TEXT("max_angular_acceleration_degrees_per_second_squared"),
			Constraint.MaxAngularAccelerationDegreesPerSecondSquared
		);
		Object->SetNumberField(
			TEXT("max_angular_jerk_degrees_per_second_cubed"),
			Constraint.MaxAngularJerkDegreesPerSecondCubed
		);
		Object->SetNumberField(
			TEXT("max_position_speed_centimeters_per_second"),
			Constraint.MaxPositionSpeedCentimetersPerSecond
		);
		Object->SetNumberField(
			TEXT("max_position_acceleration_centimeters_per_second_squared"),
			Constraint.MaxPositionAccelerationCentimetersPerSecondSquared
		);
		Object->SetNumberField(
			TEXT("max_position_jerk_centimeters_per_second_cubed"),
			Constraint.MaxPositionJerkCentimetersPerSecondCubed
		);
		Object->SetNumberField(
			TEXT("max_normalized_speed_per_second"),
			Constraint.MaxNormalizedSpeedPerSecond
		);
		Object->SetNumberField(
			TEXT("max_normalized_acceleration_per_second_squared"),
			Constraint.MaxNormalizedAccelerationPerSecondSquared
		);
		Object->SetNumberField(
			TEXT("max_normalized_jerk_per_second_cubed"),
			Constraint.MaxNormalizedJerkPerSecondCubed
		);
		Thresholds.Add(MakeShared<FJsonValueObject>(Object));
	}
	Root->SetArrayField(TEXT("approved_threshold_candidates"), Thresholds);

	TArray<FPublishedTemplateValidation> Validations;
	if (!GatherPublishedTemplateValidations(
		Profile,
		Capability.CapabilityHash,
		Validations,
		OutError
	))
	{
		return false;
	}
	TArray<TSharedPtr<FJsonValue>> TemplateReports;
	bool bAllTemplateDiagnosticsPassed = true;
	for (const FPublishedTemplateValidation& Validation : Validations)
	{
		const ULLMNPCMotionTemplate* MotionTemplate =
			Validation.MotionTemplate;
		const FLLMNPCKinematicQualityReport& Report = Validation.Report;
		bAllTemplateDiagnosticsPassed &=
			Report.bPassed;
		TSharedRef<FJsonObject> TemplateObject = MakeShared<FJsonObject>();
		TemplateObject->SetStringField(
			TEXT("template_id"),
			MotionTemplate->Metadata.TemplateId.ToString()
		);
		TemplateObject->SetStringField(
			TEXT("asset_path"),
			MotionTemplate->GetPathName()
		);
		TemplateObject->SetStringField(
			TEXT("validation_target_mode"),
			MotionTemplate->Metadata.bRequiresTarget
				? TEXT("synthetic_semantic_reference")
				: TEXT("none")
		);
		TemplateObject->SetStringField(
			TEXT("validation_style"),
			Validation.Modifiers.Style.ToString()
		);
		const FString PackageFilename =
			FPackageName::LongPackageNameToFilename(
				Validation.AssetData.PackageName.ToString(),
				FPackageName::GetAssetPackageExtension()
			);
		TemplateObject->SetStringField(
			TEXT("asset_hash"),
			HashFileMd5(PackageFilename)
		);
		TemplateObject->SetStringField(TEXT("plan_hash"), Report.PlanHash);
		TemplateObject->SetStringField(TEXT("quality_report_hash"), Report.ReportHash);
		TemplateObject->SetBoolField(TEXT("diagnostic_pass"), Report.bPassed);

		TArray<TSharedPtr<FJsonValue>> Metrics;
		for (const FLLMNPCKinematicTrackMetrics& TrackMetrics : Report.TrackMetrics)
		{
			Metrics.Add(MakeShared<FJsonValueObject>(
				BuildTrackMetricsJson(TrackMetrics)
			));
		}
		TemplateObject->SetArrayField(TEXT("track_metrics"), Metrics);
		TArray<TSharedPtr<FJsonValue>> Issues;
		for (const FLLMNPCKinematicValidationIssue& Issue : Report.Issues)
		{
			Issues.Add(MakeShared<FJsonValueObject>(BuildIssueJson(Issue)));
		}
		TemplateObject->SetArrayField(TEXT("issues"), Issues);
		TemplateReports.Add(MakeShared<FJsonValueObject>(TemplateObject));
	}
	Root->SetArrayField(TEXT("template_reports"), TemplateReports);

	FString CanonicalJson;
	if (!SerializeJson(Root, false, CanonicalJson))
	{
		OutError = TEXT("LLMNPC_MANNY_BASELINE_SERIALIZE_FAILED");
		return false;
	}
	const FString BaselineHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*CanonicalJson)
	);
	if (OutBaselineHash)
	{
		*OutBaselineHash = BaselineHash;
	}
	const FString AbsoluteFilename =
		FPaths::ConvertRelativePathToFull(OutputFilename);
	FExistingBaselineMetadata ExistingMetadata;
	LoadExistingBaselineMetadata(
		AbsoluteFilename,
		ExistingMetadata
	);
	const bool bApprovalHashCurrent =
		Profile.UpperBodyConstraints.ValidationBaselineHash == BaselineHash;
	if (
		Profile.UpperBodyConstraints.bKinematicBaselineApproved &&
		(!bApprovalHashCurrent || !bAllTemplateDiagnosticsPassed)
	)
	{
		OutError = bApprovalHashCurrent
			? TEXT("LLMNPC_MANNY_BASELINE_APPROVED_DIAGNOSTICS_FAILED")
			: TEXT("LLMNPC_MANNY_BASELINE_APPROVAL_STALE");
		return false;
	}
	const bool bApproved =
		Profile.UpperBodyConstraints.bKinematicBaselineApproved &&
		bApprovalHashCurrent &&
		bAllTemplateDiagnosticsPassed;
	const bool bExistingMetadataCurrent =
		ExistingMetadata.BaselineHash == BaselineHash;
	Root->SetStringField(
		TEXT("status"),
		bApproved ? TEXT("approved") : TEXT("pending_human_approval")
	);
	Root->SetStringField(
		TEXT("generated_at"),
		bExistingMetadataCurrent && !ExistingMetadata.GeneratedAt.IsEmpty()
			? ExistingMetadata.GeneratedAt
			: FDateTime::UtcNow().ToIso8601()
	);
	TSharedRef<FJsonObject> Approval = MakeShared<FJsonObject>();
	if (bApproved)
	{
		const bool bReuseApproval =
			bExistingMetadataCurrent &&
			ExistingMetadata.bApproved &&
			!ExistingMetadata.ApprovedAt.IsEmpty();
		Approval->SetStringField(
			TEXT("approved_by"),
			bReuseApproval && !ExistingMetadata.ApprovedBy.IsEmpty()
				? ExistingMetadata.ApprovedBy
				: TEXT("project_owner")
		);
		Approval->SetStringField(
			TEXT("approved_at"),
			bReuseApproval
				? ExistingMetadata.ApprovedAt
				: FDateTime::UtcNow().ToIso8601()
		);
		Approval->SetStringField(
			TEXT("note"),
			bReuseApproval && !ExistingMetadata.ApprovalNote.IsEmpty()
				? ExistingMetadata.ApprovalNote
				: TEXT("Manny N1 PIE visual review accepted shoulders, relaxed/curl hands, interruption/recovery, and the published gesture set.")
		);
	}
	else
	{
		Approval->SetStringField(TEXT("approved_by"), TEXT(""));
		Approval->SetStringField(TEXT("approved_at"), TEXT(""));
		Approval->SetStringField(
			TEXT("note"),
			TEXT("Thresholds remain diagnostic until a human reviews the generated distributions and Manny PIE extremes.")
		);
	}
	Root->SetObjectField(TEXT("human_approval"), Approval);
	Root->SetStringField(TEXT("baseline_hash"), BaselineHash);
	FString PrettyJson;
	if (!SerializeJson(Root, true, PrettyJson))
	{
		OutError = TEXT("LLMNPC_MANNY_BASELINE_SERIALIZE_FAILED");
		return false;
	}

	if (!IFileManager::Get().MakeDirectory(
		*FPaths::GetPath(AbsoluteFilename),
		true
	))
	{
		OutError = TEXT("LLMNPC_MANNY_BASELINE_DIRECTORY_FAILED");
		return false;
	}
	if (!FFileHelper::SaveStringToFile(
		PrettyJson,
		*AbsoluteFilename,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		OutError = TEXT("LLMNPC_MANNY_BASELINE_WRITE_FAILED");
		return false;
	}
	return true;
}

bool FLLMNPCMannyValidationBaselineExporter::CalibrateThresholdsFromPublishedTemplates(
	ULLMNPCSkeletonProfile& Profile,
	float HeadroomMultiplier,
	FString& OutError
)
{
	OutError.Reset();
	if (
		Profile.ProfileId != TEXT("ue5_manny.v1") ||
		!FMath::IsFinite(HeadroomMultiplier) ||
		HeadroomMultiplier < 1.0f
	)
	{
		OutError = TEXT("LLMNPC_MANNY_BASELINE_CALIBRATION_INPUT_INVALID");
		return false;
	}

	TArray<FPublishedTemplateValidation> Validations;
	if (!GatherPublishedTemplateValidations(
		Profile,
		FString(),
		Validations,
		OutError
	))
	{
		return false;
	}

	int32 CalibratedMetricCount = 0;
	for (const FPublishedTemplateValidation& Validation : Validations)
	{
		for (const FLLMNPCKinematicTrackMetrics& Metrics : Validation.Report.TrackMetrics)
		{
			const FName ConstraintId =
				NormalizeConstraintControlId(Metrics.ControlId);
			FLLMNPCKinematicControlConstraint* Constraint =
				Profile.ControlConstraints.FindByPredicate(
					[ConstraintId](const FLLMNPCKinematicControlConstraint& Candidate)
					{
						return Candidate.ControlId == ConstraintId;
					}
				);
			const FLLMControlDefinition* Control =
				ULLMNPCControlManifest::FindBuiltInControl(
					Metrics.ControlId
				);
			if (!Constraint || !Control)
			{
				continue;
			}

			auto RaiseLimit = [
				HeadroomMultiplier
			](float& Limit, double Observed, float Quantum)
			{
				Limit = FMath::Max(
					Limit,
					RoundObservedLimit(
						Observed,
						HeadroomMultiplier,
						Quantum
					)
				);
			};
			switch (Control->SolverType)
			{
			case ELLMControlSolverType::AdditiveRotation:
				RaiseLimit(
					Constraint->MaxAngularSpeedDegreesPerSecond,
					Metrics.MaxAbsoluteSpeed,
					10.0f
				);
				RaiseLimit(
					Constraint->MaxAngularAccelerationDegreesPerSecondSquared,
					Metrics.MaxAbsoluteAcceleration,
					100.0f
				);
				RaiseLimit(
					Constraint->MaxAngularJerkDegreesPerSecondCubed,
					Metrics.MaxAbsoluteJerk,
					1000.0f
				);
				++CalibratedMetricCount;
				break;
			case ELLMControlSolverType::LocalOffset:
				RaiseLimit(
					Constraint->MaxPositionSpeedCentimetersPerSecond,
					Metrics.MaxAbsoluteSpeed,
					10.0f
				);
				RaiseLimit(
					Constraint->MaxPositionAccelerationCentimetersPerSecondSquared,
					Metrics.MaxAbsoluteAcceleration,
					100.0f
				);
				RaiseLimit(
					Constraint->MaxPositionJerkCentimetersPerSecondCubed,
					Metrics.MaxAbsoluteJerk,
					1000.0f
				);
				++CalibratedMetricCount;
				break;
			case ELLMControlSolverType::FingerPoseBlend:
				RaiseLimit(
					Constraint->MaxNormalizedSpeedPerSecond,
					Metrics.MaxAbsoluteSpeed,
					0.5f
				);
				RaiseLimit(
					Constraint->MaxNormalizedAccelerationPerSecondSquared,
					Metrics.MaxAbsoluteAcceleration,
					10.0f
				);
				RaiseLimit(
					Constraint->MaxNormalizedJerkPerSecondCubed,
					Metrics.MaxAbsoluteJerk,
					500.0f
				);
				++CalibratedMetricCount;
				break;
			default:
				break;
			}
		}
	}

	if (CalibratedMetricCount == 0)
	{
		OutError = TEXT("LLMNPC_MANNY_BASELINE_CALIBRATION_NO_METRICS");
		return false;
	}
	Profile.UpperBodyConstraints.bKinematicBaselineApproved = false;
	Profile.UpperBodyConstraints.ValidationBaselineHash.Reset();
	Profile.MarkPackageDirty();
	return true;
}
