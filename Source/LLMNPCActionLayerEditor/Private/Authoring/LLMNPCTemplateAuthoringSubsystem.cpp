#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Authoring/LLMNPCTemplateDraftImporter.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCMotionValidator.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
FLLMNPCAuthoringOperationResult ErrorResult(FName Code, const FString& Message)
{
	FLLMNPCAuthoringOperationResult Result;
	Result.ErrorCode = Code;
	Result.Message = Message;
	return Result;
}

FLLMNPCAuthoringOperationResult SuccessResult(
	const FString& Message,
	const FString& OutputPath = FString(),
	ULLMNPCMotionTemplate* Template = nullptr
)
{
	FLLMNPCAuthoringOperationResult Result;
	Result.bSuccess = true;
	Result.Message = Message;
	Result.OutputPath = OutputPath;
	Result.TemplateAsset = Template;
	return Result;
}

void CopyTemplateData(
	const ULLMNPCMotionTemplate& Source,
	ULLMNPCMotionTemplate& Destination
)
{
	Destination.Kind = Source.Kind;
	Destination.Metadata = Source.Metadata;
	Destination.ModifierPolicy = Source.ModifierPolicy;
	Destination.ProceduralClip = Source.ProceduralClip;
	Destination.AnimationAsset = Source.AnimationAsset;
	Destination.SourceProvenanceJson = Source.SourceProvenanceJson;
	Destination.ValidationReportJson = Source.ValidationReportJson;
}

bool ParseJsonObject(
	const FString& JsonString,
	TSharedPtr<FJsonObject>& OutObject
)
{
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}

bool SerializeJsonObject(
	const TSharedRef<FJsonObject>& Object,
	FString& OutJson
)
{
	OutJson.Reset();
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
	return FJsonSerializer::Serialize(Object, Writer);
}

bool AppendReviewRecord(
	ULLMNPCMotionTemplate& Template,
	const FString& Event,
	const FString& Reviewer,
	const FString& Notes,
	bool bApproved,
	FString& OutError
)
{
	TSharedPtr<FJsonObject> Provenance;
	if (!ParseJsonObject(Template.SourceProvenanceJson, Provenance))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PROVENANCE_JSON_INVALID");
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> History;
	const TArray<TSharedPtr<FJsonValue>>* ExistingHistory = nullptr;
	if (Provenance->TryGetArrayField(TEXT("review_history"), ExistingHistory) && ExistingHistory)
	{
		History = *ExistingHistory;
	}
	TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
	Record->SetStringField(TEXT("event"), Event);
	Record->SetStringField(TEXT("reviewer"), Reviewer);
	Record->SetStringField(TEXT("notes"), Notes);
	Record->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	Record->SetBoolField(TEXT("approved"), bApproved);
	History.Add(MakeShared<FJsonValueObject>(Record));
	Provenance->SetArrayField(TEXT("review_history"), History);

	if (Event == TEXT("human_approved") || Event == TEXT("rejected"))
	{
		Provenance->SetObjectField(TEXT("human_review"), Record);
	}
	if (!SerializeJsonObject(Provenance.ToSharedRef(), Template.SourceProvenanceJson))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PROVENANCE_SERIALIZE_FAILED");
		return false;
	}
	return true;
}

ULLMNPCSkeletonProfile* FindSkeletonProfile(FName ProfileId)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")
	);
	FARFilter Filter;
	Filter.ClassPaths.Add(ULLMNPCSkeletonProfile::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	for (const FAssetData& AssetData : Assets)
	{
		ULLMNPCSkeletonProfile* Profile = Cast<ULLMNPCSkeletonProfile>(AssetData.GetAsset());
		if (Profile && Profile->ProfileId == ProfileId)
		{
			return Profile;
		}
	}
	return nullptr;
}

bool CreateTemplateAsset(
	const FString& DestinationPackagePath,
	const FString& RequestedAssetName,
	const ULLMNPCMotionTemplate& Source,
	ULLMNPCMotionTemplate*& OutAsset,
	FString& OutError
)
{
	OutAsset = nullptr;
	FString RootPath = DestinationPackagePath.TrimStartAndEnd();
	while (RootPath.EndsWith(TEXT("/")))
	{
		RootPath.LeftChopInline(1);
	}
	if (!RootPath.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(RootPath))
	{
		OutError = TEXT("LLMNPC_AUTHORING_DESTINATION_PATH_INVALID");
		return false;
	}

	const FString AssetName = ObjectTools::SanitizeObjectName(RequestedAssetName);
	if (AssetName.IsEmpty() || AssetName != RequestedAssetName)
	{
		OutError = TEXT("LLMNPC_AUTHORING_ASSET_NAME_INVALID");
		return false;
	}
	const FString PackageName = RootPath / AssetName;
	const FString ObjectPath = PackageName + TEXT(".") + AssetName;
	if (
		FindObject<ULLMNPCMotionTemplate>(nullptr, *ObjectPath) ||
		FPackageName::DoesPackageExist(PackageName)
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_ASSET_ALREADY_EXISTS");
		return false;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PACKAGE_CREATE_FAILED");
		return false;
	}
	OutAsset = NewObject<ULLMNPCMotionTemplate>(
		Package,
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional
	);
	if (!OutAsset)
	{
		OutError = TEXT("LLMNPC_AUTHORING_ASSET_CREATE_FAILED");
		return false;
	}
	CopyTemplateData(Source, *OutAsset);
	FAssetRegistryModule::AssetCreated(OutAsset);
	Package->MarkPackageDirty();
	return true;
}

bool HasPublishedTemplateId(FName TemplateId, const ULLMNPCMotionTemplate* IgnoreTemplate)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")
	);
	FARFilter Filter;
	Filter.ClassPaths.Add(ULLMNPCMotionTemplate::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	for (const FAssetData& AssetData : Assets)
	{
		const ULLMNPCMotionTemplate* Candidate = Cast<ULLMNPCMotionTemplate>(AssetData.GetAsset());
		if (
			Candidate &&
			Candidate != IgnoreTemplate &&
			Candidate->IsPublished() &&
			Candidate->Metadata.TemplateId == TemplateId
		)
		{
			return true;
		}
	}
	return false;
}

TSharedRef<FJsonObject> MakeCheck(
	const FString& Id,
	bool bPassed,
	const FString& Message
)
{
	TSharedRef<FJsonObject> Check = MakeShared<FJsonObject>();
	Check->SetStringField(TEXT("id"), Id);
	Check->SetBoolField(TEXT("passed"), bPassed);
	Check->SetStringField(TEXT("message"), Message);
	return Check;
}
}

void ULLMNPCTemplateAuthoringSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Error;
	EnsureAuthoringDirectories(Error);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::BuildAuthoringContextFromUEPIProfile(
	const FString& ReconstructionProfileFilePath,
	const FString& OutputFilePath
)
{
	FString DirectoryError;
	if (!EnsureAuthoringDirectories(DirectoryError))
	{
		return ErrorResult(TEXT("LLMNPC_AUTHORING_DIRECTORY_FAILED"), DirectoryError);
	}

	FLLMNPCUEPIReconstructionSummary Summary;
	FString ContextJson;
	FString Error;
	if (!FLLMNPCUEPIArtifactAdapter::LoadReconstructionProfile(
		ReconstructionProfileFilePath,
		Summary,
		ContextJson,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}

	FString ResolvedOutputPath = OutputFilePath.TrimStartAndEnd();
	if (ResolvedOutputPath.IsEmpty())
	{
		const FString BaseName = FPaths::GetBaseFilename(Summary.SequencePath);
		ResolvedOutputPath = FPaths::Combine(
			FPaths::GetPath(GetDraftDirectory()),
			TEXT("Contexts"),
			FString::Printf(TEXT("%s_authoring_context.json"), *BaseName)
		);
	}
	ResolvedOutputPath = FPaths::ConvertRelativePathToFull(ResolvedOutputPath);
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(ResolvedOutputPath), true))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_CONTEXT_DIRECTORY_FAILED"),
			TEXT("Could not create the authoring context directory.")
		);
	}
	if (!FFileHelper::SaveStringToFile(ContextJson, *ResolvedOutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_CONTEXT_WRITE_FAILED"),
			TEXT("Could not write the authoring context file.")
		);
	}

	FLLMNPCAuthoringOperationResult Result = SuccessResult(
		TEXT("UEPI reconstruction profile converted to an authoring context."),
		ResolvedOutputPath
	);
	Result.ReconstructionSummary = Summary;
	return Result;
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::ImportDraftFromFile(
	const FString& DraftFilePath,
	const FString& DestinationPackagePath
)
{
	FString DraftJson;
	if (!FFileHelper::LoadFileToString(DraftJson, *DraftFilePath))
	{
		return ErrorResult(
			TEXT("LLMNPC_DRAFT_FILE_READ_FAILED"),
			TEXT("Could not read the Draft JSON file.")
		);
	}
	return ImportDraftJson(DraftJson, DestinationPackagePath);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::ImportDraftJson(
	const FString& DraftJson,
	const FString& DestinationPackagePath
)
{
	FString DirectoryError;
	if (!EnsureAuthoringDirectories(DirectoryError))
	{
		return ErrorResult(TEXT("LLMNPC_AUTHORING_DIRECTORY_FAILED"), DirectoryError);
	}

	ULLMNPCMotionTemplate* ParsedTemplate = NewObject<ULLMNPCMotionTemplate>(GetTransientPackage());
	FLLMNPCParsedDraftInfo ParsedInfo;
	FString Error;
	if (!FLLMNPCTemplateDraftImporter::ParseDraftJson(
		DraftJson,
		*ParsedTemplate,
		ParsedInfo,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}

	const FString DraftHash = FLLMNPCUEPIArtifactAdapter::HashJson(DraftJson);
	FString SafeHash = DraftHash;
	SafeHash.ReplaceInline(TEXT(":"), TEXT("_"));
	const FString SourceCopyPath = FPaths::Combine(
		GetDraftDirectory(),
		FString::Printf(TEXT("%s_%s.json"), *ParsedInfo.AssetName, *SafeHash.Right(12))
	);
	if (!FFileHelper::SaveStringToFile(
		DraftJson,
		*SourceCopyPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		return ErrorResult(
			TEXT("LLMNPC_DRAFT_SOURCE_COPY_FAILED"),
			TEXT("Could not preserve the imported Draft source.")
		);
	}

	TSharedPtr<FJsonObject> Provenance;
	if (!ParseJsonObject(ParsedTemplate->SourceProvenanceJson, Provenance))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PROVENANCE_JSON_INVALID"),
			TEXT("Draft provenance could not be parsed after validation.")
		);
	}
	TSharedRef<FJsonObject> ImportRecord = MakeShared<FJsonObject>();
	ImportRecord->SetStringField(TEXT("schema_version"), TEXT("llmnpc.draft_import.v1"));
	ImportRecord->SetStringField(TEXT("draft_content_hash"), DraftHash);
	ImportRecord->SetStringField(TEXT("draft_source_copy_path"), FPaths::ConvertRelativePathToFull(SourceCopyPath));
	ImportRecord->SetStringField(TEXT("imported_at_utc"), FDateTime::UtcNow().ToIso8601());
	Provenance->SetObjectField(TEXT("import_record"), ImportRecord);
	if (!SerializeJsonObject(Provenance.ToSharedRef(), ParsedTemplate->SourceProvenanceJson))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PROVENANCE_SERIALIZE_FAILED"),
			TEXT("Draft provenance could not be updated with the import record.")
		);
	}

	ULLMNPCMotionTemplate* Asset = nullptr;
	if (!CreateTemplateAsset(
		DestinationPackagePath,
		ParsedInfo.AssetName,
		*ParsedTemplate,
		Asset,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}
	if (!SaveTemplateAsset(Asset, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	return SuccessResult(
		TEXT("Draft imported as a Generated template. It is not runtime-selectable."),
		Asset->GetPathName(),
		Asset
	);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::GenerateQualityReport(
	ULLMNPCMotionTemplate* Template,
	const FString& ReconstructionProfileFilePath,
	const FString& FullPoseArtifactFilePath
)
{
	if (!Template || Template->IsPublished())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_QUALITY_TEMPLATE_INVALID"),
			TEXT("Quality reports can only be generated for non-Published templates.")
		);
	}

	bool bAllPassed = true;
	TArray<TSharedPtr<FJsonValue>> Checks;
	TArray<TSharedPtr<FJsonValue>> Warnings;
	auto AddCheck = [&Checks, &bAllPassed](const FString& Id, bool bPassed, const FString& Message)
	{
		Checks.Add(MakeShared<FJsonValueObject>(MakeCheck(Id, bPassed, Message)));
		bAllPassed &= bPassed;
	};

	FString ValidationError;
	const bool bTemplateValid = Template->ValidateTemplate(ValidationError);
	AddCheck(
		TEXT("template_structure"),
		bTemplateValid,
		bTemplateValid ? TEXT("Template structure is valid.") : ValidationError
	);

	FLLMMotionPlan Plan;
	Plan.Intent = Template->Metadata.PublicActionId.ToString();
	Plan.Clip = Template->ProceduralClip;
	ULLMNPCMotionValidator* Validator = NewObject<ULLMNPCMotionValidator>(this);
	const FLLMMotionValidationResult MotionResult = Validator->ValidateAndClamp(
		Plan,
		ELLMNPCMotionValidationSource::PublishedTemplate
	);
	AddCheck(
		TEXT("motion_validator"),
		MotionResult.bValid,
		MotionResult.bValid ? TEXT("Motion tracks pass the Published Template trust boundary.") : MotionResult.ErrorMessage
	);

	ULLMNPCSkeletonProfile* SkeletonProfile = FindSkeletonProfile(Template->Metadata.SkeletonProfileId);
	FString ProfileValidationError;
	const bool bProfileValid = SkeletonProfile && SkeletonProfile->ValidateProfile(ProfileValidationError);
	AddCheck(
		TEXT("skeleton_profile"),
		bProfileValid,
		bProfileValid ? TEXT("Skeleton Profile exists and validates.") :
			(ProfileValidationError.IsEmpty() ? TEXT("Skeleton Profile was not found.") : ProfileValidationError)
	);

	TSharedPtr<FJsonObject> Provenance;
	const bool bProvenanceJsonValid = ParseJsonObject(Template->SourceProvenanceJson, Provenance);
	AddCheck(
		TEXT("provenance_json"),
		bProvenanceJsonValid,
		bProvenanceJsonValid ? TEXT("Provenance JSON parses.") : TEXT("Provenance JSON is invalid.")
	);

	FLLMNPCUEPIReconstructionSummary Summary;
	FString AuthoringContext;
	FString ArtifactError;
	bool bArtifactValid = false;
	if (!ReconstructionProfileFilePath.TrimStartAndEnd().IsEmpty())
	{
		bArtifactValid = FLLMNPCUEPIArtifactAdapter::LoadReconstructionProfile(
			ReconstructionProfileFilePath,
			Summary,
			AuthoringContext,
			ArtifactError
		);
	}
	AddCheck(
		TEXT("reconstruction_profile"),
		bArtifactValid,
		bArtifactValid ? TEXT("UEPI Reconstruction Profile validates.") :
			(ArtifactError.IsEmpty() ? TEXT("A Reconstruction Profile is required.") : ArtifactError)
	);

	FString ExpectedReconstructionHash;
	if (bProvenanceJsonValid)
	{
		Provenance->TryGetStringField(TEXT("reconstruction_profile_hash"), ExpectedReconstructionHash);
	}
	const bool bArtifactHashMatches =
		bArtifactValid &&
		!ExpectedReconstructionHash.IsEmpty() &&
		ExpectedReconstructionHash == Summary.ProfileContentHash;
	AddCheck(
		TEXT("reconstruction_hash"),
		bArtifactHashMatches,
		bArtifactHashMatches
			? TEXT("Draft provenance matches the source Reconstruction Profile hash.")
			: TEXT("Draft provenance does not match the supplied Reconstruction Profile hash.")
	);

	FString FullPoseValidation = TEXT("not_requested");
	if (!FullPoseArtifactFilePath.TrimStartAndEnd().IsEmpty())
	{
		FString FullPoseJson;
		const bool bFullPoseRead = FFileHelper::LoadFileToString(
			FullPoseJson,
			*FullPoseArtifactFilePath
		);
		FString FullPoseError;
		const bool bFullPoseValid =
			bFullPoseRead &&
			bArtifactValid &&
			FLLMNPCUEPIArtifactAdapter::ValidateFullPoseArtifact(
				FullPoseJson,
				Summary,
				FullPoseError
			);
		FString ExpectedFullPoseHash;
		if (bProvenanceJsonValid)
		{
			Provenance->TryGetStringField(TEXT("full_pose_artifact_hash"), ExpectedFullPoseHash);
		}
		const FString ActualFullPoseHash = bFullPoseRead
			? FLLMNPCUEPIArtifactAdapter::HashJson(FullPoseJson)
			: FString();
		const bool bFullPoseHashMatches =
			!ExpectedFullPoseHash.IsEmpty() &&
			ExpectedFullPoseHash == ActualFullPoseHash;
		AddCheck(
			TEXT("full_pose_artifact"),
			bFullPoseValid,
			bFullPoseValid ? TEXT("Full Pose Artifact source identity and samples validate.") :
				(FullPoseError.IsEmpty() ? TEXT("Full Pose Artifact could not be read.") : FullPoseError)
		);
		AddCheck(
			TEXT("full_pose_hash"),
			bFullPoseHashMatches,
			bFullPoseHashMatches
				? TEXT("Full Pose Artifact matches the Draft provenance hash.")
				: TEXT("Full Pose Artifact does not match the Draft provenance hash.")
		);
		FullPoseValidation = bFullPoseValid && bFullPoseHashMatches
			? TEXT("artifact_checked")
			: TEXT("not_requested");
	}
	else if (bArtifactValid && !Summary.FullPoseArtifactUri.IsEmpty())
	{
		FullPoseValidation = TEXT("manifest_only");
		Warnings.Add(MakeShared<FJsonValueString>(
			TEXT("Full Pose Artifact was not loaded; validation used its manifest reference only.")
		));
	}

	if (bArtifactValid)
	{
		const float DurationRatio = Template->ProceduralClip.Duration / Summary.PlayLengthSeconds;
		if (DurationRatio < 0.2f || DurationRatio > 2.0f)
		{
			Warnings.Add(MakeShared<FJsonValueString>(
				TEXT("Template duration differs substantially from the source animation duration.")
			));
		}
	}

	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("schema_version"), TEXT("llmnpc.template_quality_report.v1"));
	Report->SetStringField(TEXT("status"), bAllPassed ? TEXT("pass") : TEXT("fail"));
	Report->SetStringField(TEXT("template_id"), Template->Metadata.TemplateId.ToString());
	Report->SetStringField(TEXT("template_content_hash"), BuildTemplateContentHash(*Template));
	Report->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());
	Report->SetStringField(TEXT("source_reconstruction_hash"), Summary.ProfileContentHash);
	Report->SetStringField(TEXT("full_pose_validation"), FullPoseValidation);
	Report->SetArrayField(TEXT("checks"), Checks);
	Report->SetArrayField(TEXT("warnings"), Warnings);
	Report->SetBoolField(TEXT("manual_review_required"), true);
	if (!SerializeJsonObject(Report, Template->ValidationReportJson))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_REPORT_SERIALIZE_FAILED"),
			TEXT("Could not serialize the quality report.")
		);
	}

	FString SaveError;
	if (!SaveTemplateAsset(Template, SaveError))
	{
		return ErrorResult(FName(*SaveError), SaveError);
	}
	FString DirectoryError;
	if (!EnsureAuthoringDirectories(DirectoryError))
	{
		return ErrorResult(TEXT("LLMNPC_AUTHORING_DIRECTORY_FAILED"), DirectoryError);
	}
	const FString ReportPath = FPaths::Combine(
		GetReportDirectory(),
		Template->Metadata.TemplateId.ToString().Replace(TEXT("."), TEXT("_")) + TEXT("_quality.json")
	);
	if (!FFileHelper::SaveStringToFile(
		Template->ValidationReportJson,
		*ReportPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_REPORT_WRITE_FAILED"),
			TEXT("Quality report was generated but could not be written to disk.")
		);
	}

	FLLMNPCAuthoringOperationResult Result = bAllPassed
		? SuccessResult(TEXT("Quality report passed; manual preview is still required."), ReportPath, Template)
		: ErrorResult(TEXT("LLMNPC_AUTHORING_QUALITY_FAILED"), TEXT("One or more quality checks failed."));
	Result.OutputPath = ReportPath;
	Result.TemplateAsset = Template;
	Result.ReconstructionSummary = Summary;
	return Result;
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::MarkTemplatePreviewed(
	ULLMNPCMotionTemplate* Template,
	const FString& PreviewNotes
)
{
	if (!Template || Template->Metadata.ReviewState != ELLMNPCTemplateReviewState::Generated)
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PREVIEW_STATE_INVALID"),
			TEXT("Only a Generated template can be marked Previewed.")
		);
	}
	FString Error;
	if (!HasCurrentPassingQualityReport(*Template, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	const FString Notes = PreviewNotes.TrimStartAndEnd();
	if (Notes.IsEmpty())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PREVIEW_NOTES_REQUIRED"),
			TEXT("Preview notes are required.")
		);
	}
	if (!AppendReviewRecord(*Template, TEXT("previewed"), TEXT("preview_operator"), Notes, false, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	Template->Metadata.ReviewState = ELLMNPCTemplateReviewState::Previewed;
	if (!SaveTemplateAsset(Template, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	return SuccessResult(TEXT("Template marked Previewed; human approval is still required."), Template->GetPathName(), Template);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::ApproveTemplate(
	ULLMNPCMotionTemplate* Template,
	const FString& Reviewer,
	const FString& ReviewNotes
)
{
	if (!Template || Template->Metadata.ReviewState != ELLMNPCTemplateReviewState::Previewed)
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_APPROVAL_STATE_INVALID"),
			TEXT("Only a Previewed template can be approved.")
		);
	}
	const FString CleanReviewer = Reviewer.TrimStartAndEnd();
	const FString CleanNotes = ReviewNotes.TrimStartAndEnd();
	if (CleanReviewer.IsEmpty() || CleanNotes.IsEmpty())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_APPROVAL_IDENTITY_REQUIRED"),
			TEXT("Reviewer identity and review notes are required.")
		);
	}
	FString Error;
	if (
		!HasCurrentPassingQualityReport(*Template, Error) ||
		!AppendReviewRecord(*Template, TEXT("human_approved"), CleanReviewer, CleanNotes, true, Error)
	)
	{
		return ErrorResult(FName(*Error), Error);
	}
	Template->Metadata.ReviewState = ELLMNPCTemplateReviewState::HumanApproved;
	if (!SaveTemplateAsset(Template, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	return SuccessResult(TEXT("Template is HumanApproved and eligible for an explicit Publish operation."), Template->GetPathName(), Template);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::RejectTemplate(
	ULLMNPCMotionTemplate* Template,
	const FString& Reviewer,
	const FString& RejectionReason
)
{
	if (!Template || Template->IsPublished())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_REJECT_STATE_INVALID"),
			TEXT("A Published template cannot be rejected through the Draft workflow.")
		);
	}
	const FString CleanReviewer = Reviewer.TrimStartAndEnd();
	const FString CleanReason = RejectionReason.TrimStartAndEnd();
	if (CleanReviewer.IsEmpty() || CleanReason.IsEmpty())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_REJECTION_REASON_REQUIRED"),
			TEXT("Reviewer identity and rejection reason are required.")
		);
	}
	FString Error;
	if (!AppendReviewRecord(*Template, TEXT("rejected"), CleanReviewer, CleanReason, false, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	Template->Metadata.ReviewState = ELLMNPCTemplateReviewState::Rejected;
	if (!SaveTemplateAsset(Template, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}

	TSharedPtr<FJsonObject> Provenance;
	const TSharedPtr<FJsonObject>* ImportRecord = nullptr;
	FString SourceCopyPath;
	if (
		ParseJsonObject(Template->SourceProvenanceJson, Provenance) &&
		Provenance->TryGetObjectField(TEXT("import_record"), ImportRecord) &&
		ImportRecord && ImportRecord->IsValid()
	)
	{
		(*ImportRecord)->TryGetStringField(TEXT("draft_source_copy_path"), SourceCopyPath);
	}
	FString RejectedCopyPath;
	if (!SourceCopyPath.IsEmpty() && IFileManager::Get().FileExists(*SourceCopyPath))
	{
		RejectedCopyPath = FPaths::Combine(GetRejectedDirectory(), FPaths::GetCleanFilename(SourceCopyPath));
		IFileManager::Get().Copy(*RejectedCopyPath, *SourceCopyPath, true, true);
	}
	return SuccessResult(TEXT("Template rejected and excluded from runtime publication."), RejectedCopyPath, Template);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::PublishTemplate(
	ULLMNPCMotionTemplate* Template,
	const FString& DestinationPackagePath
)
{
	FString Error;
	if (!CanPublishTemplate(Template, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	if (HasPublishedTemplateId(Template->Metadata.TemplateId, Template))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_TEMPLATE_ID_ALREADY_PUBLISHED"),
			TEXT("A Published template already uses this Template ID. Increment the semantic and template version first.")
		);
	}

	FString PublishedAssetName = Template->GetName();
	if (PublishedAssetName.StartsWith(TEXT("DT_")))
	{
		PublishedAssetName = TEXT("MT_") + PublishedAssetName.RightChop(3);
	}
	else if (!PublishedAssetName.StartsWith(TEXT("MT_")))
	{
		PublishedAssetName = TEXT("MT_") + PublishedAssetName;
	}
	ULLMNPCMotionTemplate* PublishCandidate = DuplicateObject<ULLMNPCMotionTemplate>(
		Template,
		GetTransientPackage()
	);
	PublishCandidate->Metadata.ReviewState = ELLMNPCTemplateReviewState::Published;
	FString ValidationError;
	if (!PublishCandidate->ValidateTemplate(ValidationError))
	{
		return ErrorResult(FName(*ValidationError), ValidationError);
	}

	ULLMNPCMotionTemplate* PublishedAsset = nullptr;
	if (!CreateTemplateAsset(
		DestinationPackagePath,
		PublishedAssetName,
		*PublishCandidate,
		PublishedAsset,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}
	if (!SaveTemplateAsset(PublishedAsset, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}

	UAssetManager::Get().ScanPathForPrimaryAssets(
		TEXT("LLMNPCTemplate"),
		DestinationPackagePath,
		ULLMNPCMotionTemplate::StaticClass(),
		false
	);
	return SuccessResult(
		TEXT("HumanApproved template copied to the Published runtime library."),
		PublishedAsset->GetPathName(),
		PublishedAsset
	);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::PreviewTemplateOnActor(
	ULLMNPCMotionTemplate* Template,
	AActor* PreviewActor
)
{
	if (!Template || !PreviewActor || !PreviewActor->GetWorld() || !PreviewActor->GetWorld()->IsGameWorld())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PREVIEW_REQUIRES_PIE"),
			TEXT("Preview requires a template and an actor in a running PIE or game world.")
		);
	}
	ULLMNPCMotionComponent* MotionComponent = PreviewActor->FindComponentByClass<ULLMNPCMotionComponent>();
	if (!MotionComponent)
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PREVIEW_MOTION_COMPONENT_MISSING"),
			TEXT("Preview actor has no LLM NPC Motion Component.")
		);
	}
	FLLMMotionPlan Plan;
	FString Error;
	if (!CompileTemplateForPreview(*Template, Plan, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	if (!MotionComponent->SubmitCompiledTemplatePlan(MoveTemp(Plan)))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PREVIEW_SUBMIT_FAILED"),
			MotionComponent->LastValidationError
		);
	}
	return SuccessResult(TEXT("Draft submitted to the PIE preview actor."), PreviewActor->GetPathName(), Template);
}

bool ULLMNPCTemplateAuthoringSubsystem::CanPublishTemplate(
	const ULLMNPCMotionTemplate* Template,
	FString& OutError
) const
{
	OutError.Reset();
	if (!Template || Template->Metadata.ReviewState != ELLMNPCTemplateReviewState::HumanApproved)
	{
		OutError = TEXT("LLMNPC_AUTHORING_HUMAN_APPROVAL_REQUIRED");
		return false;
	}
	if (!HasCurrentPassingQualityReport(*Template, OutError))
	{
		return false;
	}
	return ValidateProvenanceForPublish(*Template, OutError);
}

FString ULLMNPCTemplateAuthoringSubsystem::GetDraftDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("LLMNPCActionLayer/Authoring/Drafts")
	));
}

FString ULLMNPCTemplateAuthoringSubsystem::GetReportDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("LLMNPCActionLayer/Authoring/Reports")
	));
}

FString ULLMNPCTemplateAuthoringSubsystem::GetRejectedDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("LLMNPCActionLayer/Authoring/Rejected")
	));
}

bool ULLMNPCTemplateAuthoringSubsystem::CompileTemplateForPreview(
	const ULLMNPCMotionTemplate& Template,
	FLLMMotionPlan& OutPlan,
	FString& OutError
)
{
	if (!HasCurrentPassingQualityReport(Template, OutError))
	{
		return false;
	}
	ULLMNPCSkeletonProfile* Profile = FindSkeletonProfile(Template.Metadata.SkeletonProfileId);
	if (!Profile)
	{
		OutError = TEXT("LLMNPC_AUTHORING_SKELETON_PROFILE_NOT_FOUND");
		return false;
	}
	ULLMNPCMotionTemplate* PreviewCopy = DuplicateObject<ULLMNPCMotionTemplate>(
		&Template,
		GetTransientPackage()
	);
	PreviewCopy->Metadata.ReviewState = ELLMNPCTemplateReviewState::Published;
	return FLLMNPCTemplateCompiler::Compile(
		*PreviewCopy,
		FLLMNPCTemplateModifiers(),
		*Profile,
		OutPlan,
		OutError
	);
}

bool ULLMNPCTemplateAuthoringSubsystem::EnsureAuthoringDirectories(FString& OutError)
{
	OutError.Reset();
	const TArray<FString> Directories = {
		GetDraftDirectory(),
		GetReportDirectory(),
		GetRejectedDirectory(),
		FPaths::Combine(FPaths::GetPath(GetDraftDirectory()), TEXT("Contexts"))
	};
	for (const FString& Directory : Directories)
	{
		if (!IFileManager::Get().MakeDirectory(*Directory, true))
		{
			OutError = FString::Printf(TEXT("Could not create authoring directory: %s"), *Directory);
			return false;
		}
	}
	return true;
}

bool ULLMNPCTemplateAuthoringSubsystem::SaveTemplateAsset(
	ULLMNPCMotionTemplate* Template,
	FString& OutError
)
{
	OutError.Reset();
	if (!Template)
	{
		OutError = TEXT("LLMNPC_AUTHORING_TEMPLATE_MISSING");
		return false;
	}
	UPackage* Package = Template->GetOutermost();
	if (!Package || Package == GetTransientPackage())
	{
		return true;
	}
	Package->MarkPackageDirty();
	const FString PackageName = Package->GetName();
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PACKAGE_NAME_INVALID");
		return false;
	}
	const FString Filename = FPackageName::LongPackageNameToFilename(
		PackageName,
		FPackageName::GetAssetPackageExtension()
	);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(Package, Template, *Filename, SaveArgs))
	{
		OutError = TEXT("LLMNPC_AUTHORING_ASSET_SAVE_FAILED");
		return false;
	}
	return true;
}

FString ULLMNPCTemplateAuthoringSubsystem::BuildTemplateContentHash(
	const ULLMNPCMotionTemplate& Template
)
{
	FString MetadataJson;
	FString ModifierJson;
	FString ClipJson;
	FString StableProvenanceJson = Template.SourceProvenanceJson;
	FLLMNPCTemplateMetadata StableMetadata = Template.Metadata;
	StableMetadata.ReviewState = ELLMNPCTemplateReviewState::Generated;
	FJsonObjectConverter::UStructToJsonObjectString(StableMetadata, MetadataJson);
	FJsonObjectConverter::UStructToJsonObjectString(Template.ModifierPolicy, ModifierJson);
	FJsonObjectConverter::UStructToJsonObjectString(Template.ProceduralClip, ClipJson);
	TSharedPtr<FJsonObject> StableProvenance;
	if (ParseJsonObject(Template.SourceProvenanceJson, StableProvenance))
	{
		StableProvenance->RemoveField(TEXT("review_history"));
		StableProvenance->RemoveField(TEXT("human_review"));
		SerializeJsonObject(StableProvenance.ToSharedRef(), StableProvenanceJson);
	}
	return FLLMNPCUEPIArtifactAdapter::HashJson(
		FString::Printf(
			TEXT("%d|%s|%s|%s|%s"),
			static_cast<int32>(Template.Kind),
			*MetadataJson,
			*ModifierJson,
			*ClipJson,
			*StableProvenanceJson
		)
	);
}

bool ULLMNPCTemplateAuthoringSubsystem::HasCurrentPassingQualityReport(
	const ULLMNPCMotionTemplate& Template,
	FString& OutError
)
{
	TSharedPtr<FJsonObject> Report;
	if (!ParseJsonObject(Template.ValidationReportJson, Report))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PASSING_QUALITY_REPORT_REQUIRED");
		return false;
	}
	FString Schema;
	FString Status;
	FString ReportHash;
	if (
		!Report->TryGetStringField(TEXT("schema_version"), Schema) ||
		Schema != TEXT("llmnpc.template_quality_report.v1") ||
		!Report->TryGetStringField(TEXT("status"), Status) ||
		Status != TEXT("pass") ||
		!Report->TryGetStringField(TEXT("template_content_hash"), ReportHash)
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PASSING_QUALITY_REPORT_REQUIRED");
		return false;
	}
	if (ReportHash != BuildTemplateContentHash(Template))
	{
		OutError = TEXT("LLMNPC_AUTHORING_QUALITY_REPORT_STALE");
		return false;
	}
	return true;
}

bool ULLMNPCTemplateAuthoringSubsystem::ValidateProvenanceForPublish(
	const ULLMNPCMotionTemplate& Template,
	FString& OutError
)
{
	TSharedPtr<FJsonObject> Provenance;
	if (!ParseJsonObject(Template.SourceProvenanceJson, Provenance))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PROVENANCE_JSON_INVALID");
		return false;
	}
	const TSharedPtr<FJsonObject>* License = nullptr;
	const TSharedPtr<FJsonObject>* HumanReview = nullptr;
	if (
		!Provenance->TryGetObjectField(TEXT("source_license"), License) || !License || !License->IsValid() ||
		!Provenance->TryGetObjectField(TEXT("human_review"), HumanReview) || !HumanReview || !HumanReview->IsValid()
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_LICENSE_OR_REVIEW_MISSING");
		return false;
	}
	FString Identifier;
	FString Holder;
	FString Reviewer;
	FString Notes;
	FString Timestamp;
	bool bApproved = false;
	bool bRedistributionAllowed = false;
	if (
		!(*License)->TryGetStringField(TEXT("identifier"), Identifier) || Identifier.TrimStartAndEnd().IsEmpty() ||
		!(*License)->TryGetStringField(TEXT("holder"), Holder) || Holder.TrimStartAndEnd().IsEmpty() ||
		!(*License)->TryGetBoolField(TEXT("redistribution_allowed"), bRedistributionAllowed) ||
		!(*HumanReview)->TryGetStringField(TEXT("reviewer"), Reviewer) || Reviewer.TrimStartAndEnd().IsEmpty() ||
		!(*HumanReview)->TryGetStringField(TEXT("notes"), Notes) || Notes.TrimStartAndEnd().IsEmpty() ||
		!(*HumanReview)->TryGetStringField(TEXT("timestamp_utc"), Timestamp) || Timestamp.IsEmpty() ||
		!(*HumanReview)->TryGetBoolField(TEXT("approved"), bApproved) || !bApproved
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_LICENSE_OR_REVIEW_INVALID");
		return false;
	}
	return true;
}
