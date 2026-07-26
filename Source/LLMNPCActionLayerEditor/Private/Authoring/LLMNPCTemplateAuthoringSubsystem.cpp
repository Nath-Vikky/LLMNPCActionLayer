#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Authoring/LLMNPCAnimationTemplateDraftImporter.h"
#include "Authoring/LLMNPCTemplateDraftImporter.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "JsonObjectConverter.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCMotionValidator.h"
#include "LLMNPCSettings.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCActionVocabulary.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"
#include "Templates/LLMNPCTemplateSearchIndex.h"
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

FLLMNPCAuthoringOperationResult PublicActionSuccessResult(
	const FString& Message,
	const FString& OutputPath,
	ULLMNPCPublicActionDefinition* Definition
)
{
	FLLMNPCAuthoringOperationResult Result;
	Result.bSuccess = true;
	Result.Message = Message;
	Result.OutputPath = OutputPath;
	Result.PublicActionAsset = Definition;
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
	Destination.AnimationPlayback = Source.AnimationPlayback;
	Destination.SourceProvenanceJson = Source.SourceProvenanceJson;
	Destination.ValidationReportJson = Source.ValidationReportJson;
}

void CopyPublicActionData(
	const ULLMNPCPublicActionDefinition& Source,
	ULLMNPCPublicActionDefinition& Destination
)
{
	Destination.PublicActionId = Source.PublicActionId;
	Destination.SemanticVersion = Source.SemanticVersion;
	Destination.DefinitionRevision = Source.DefinitionRevision;
	Destination.DisplayName = Source.DisplayName;
	Destination.SelectionSummary = Source.SelectionSummary;
	Destination.SuitableWhen = Source.SuitableWhen;
	Destination.AvoidWhen = Source.AvoidWhen;
	Destination.SemanticEffectTags = Source.SemanticEffectTags;
	Destination.TargetCategoryTags = Source.TargetCategoryTags;
	Destination.GestureFamily = Source.GestureFamily;
	Destination.DefaultStyle = Source.DefaultStyle;
	Destination.IncompatibleActionFamilies = Source.IncompatibleActionFamilies;
	Destination.SearchKeywords = Source.SearchKeywords;
	Destination.bRequiresTarget = Source.bRequiresTarget;
	Destination.CatalogSchemaVersion = Source.CatalogSchemaVersion;
	Destination.ReviewState = Source.ReviewState;
	Destination.ContentHash = Source.ContentHash;
	Destination.ReviewRecordJson = Source.ReviewRecordJson;
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

bool AppendPublicActionReviewRecord(
	ULLMNPCPublicActionDefinition& Definition,
	const FString& Event,
	const FString& Reviewer,
	const FString& Notes,
	bool bApproved,
	FString& OutError
)
{
	TSharedPtr<FJsonObject> ReviewRecord;
	if (
		Definition.ReviewRecordJson.TrimStartAndEnd().IsEmpty() ||
		!ParseJsonObject(Definition.ReviewRecordJson, ReviewRecord)
	)
	{
		ReviewRecord = MakeShared<FJsonObject>();
		ReviewRecord->SetStringField(
			TEXT("schema_version"),
			TEXT("llmnpc.public_action_review.v1")
		);
	}

	TArray<TSharedPtr<FJsonValue>> History;
	const TArray<TSharedPtr<FJsonValue>>* ExistingHistory = nullptr;
	if (
		ReviewRecord->TryGetArrayField(TEXT("history"), ExistingHistory) &&
		ExistingHistory
	)
	{
		History = *ExistingHistory;
	}
	TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
	Entry->SetStringField(TEXT("event"), Event);
	Entry->SetStringField(TEXT("reviewer"), Reviewer);
	Entry->SetStringField(TEXT("notes"), Notes);
	Entry->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
	Entry->SetBoolField(TEXT("approved"), bApproved);
	History.Add(MakeShared<FJsonValueObject>(Entry));
	ReviewRecord->SetArrayField(TEXT("history"), History);
	if (Event == TEXT("human_approved") || Event == TEXT("rejected"))
	{
		ReviewRecord->SetObjectField(TEXT("human_review"), Entry);
	}
	if (!SerializeJsonObject(ReviewRecord.ToSharedRef(), Definition.ReviewRecordJson))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_REVIEW_SERIALIZE_FAILED");
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

bool CreatePublicActionAsset(
	const FString& DestinationPackagePath,
	const FString& RequestedAssetName,
	const ULLMNPCPublicActionDefinition& Source,
	ULLMNPCPublicActionDefinition*& OutAsset,
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
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_DESTINATION_PATH_INVALID");
		return false;
	}

	const FString AssetName = ObjectTools::SanitizeObjectName(RequestedAssetName);
	if (AssetName.IsEmpty() || AssetName != RequestedAssetName)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_ASSET_NAME_INVALID");
		return false;
	}
	const FString PackageName = RootPath / AssetName;
	const FString ObjectPath = PackageName + TEXT(".") + AssetName;
	if (
		FindObject<ULLMNPCPublicActionDefinition>(nullptr, *ObjectPath) ||
		FPackageName::DoesPackageExist(PackageName)
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_ASSET_ALREADY_EXISTS");
		return false;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_PACKAGE_CREATE_FAILED");
		return false;
	}
	OutAsset = NewObject<ULLMNPCPublicActionDefinition>(
		Package,
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional
	);
	if (!OutAsset)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_ASSET_CREATE_FAILED");
		return false;
	}
	CopyPublicActionData(Source, *OutAsset);
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

bool HasPublishedPublicActionVersion(
	const ULLMNPCPublicActionDefinition& Definition,
	const ULLMNPCPublicActionDefinition* IgnoreDefinition
)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")
	);
	FARFilter Filter;
	Filter.ClassPaths.Add(ULLMNPCPublicActionDefinition::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	for (const FAssetData& AssetData : Assets)
	{
		const ULLMNPCPublicActionDefinition* Candidate =
			Cast<ULLMNPCPublicActionDefinition>(AssetData.GetAsset());
		if (
			Candidate &&
			Candidate != IgnoreDefinition &&
			Candidate->IsPublished() &&
			Candidate->PublicActionId == Definition.PublicActionId &&
			Candidate->SemanticVersion == Definition.SemanticVersion &&
			Candidate->DefinitionRevision == Definition.DefinitionRevision
		)
		{
			return true;
		}
	}
	return false;
}

bool LoadPublishedDefinitionForAction(
	FName PublicActionId,
	ULLMNPCActionVocabulary*& OutVocabulary,
	const ULLMNPCPublicActionDefinition*& OutDefinition,
	FString& OutError
)
{
	OutVocabulary = nullptr;
	OutDefinition = nullptr;
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings)
	{
		OutError = TEXT("LLMNPC_AUTHORING_SETTINGS_MISSING");
		return false;
	}
	OutVocabulary = Settings->ActionVocabulary.LoadSynchronous();
	if (!OutVocabulary || !OutVocabulary->ValidateVocabulary(OutError))
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("LLMNPC_AUTHORING_VOCABULARY_MISSING");
		}
		return false;
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")
	);
	FARFilter Filter;
	Filter.ClassPaths.Add(ULLMNPCPublicActionDefinition::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	TArray<ULLMNPCPublicActionDefinition*> Definitions;
	for (const FAssetData& AssetData : Assets)
	{
		if (ULLMNPCPublicActionDefinition* Definition =
			Cast<ULLMNPCPublicActionDefinition>(AssetData.GetAsset()))
		{
			Definitions.Add(Definition);
		}
	}
	FLLMNPCTemplateSearchIndex DefinitionIndex;
	if (!DefinitionIndex.Build({}, Definitions, OutVocabulary, {}))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_CATALOG_INVALID");
		return false;
	}
	for (const FLLMNPCCatalogDiagnostic& Diagnostic : DefinitionIndex.GetDiagnostics())
	{
		if (
			Diagnostic.AssetPath.Contains(PublicActionId.ToString()) ||
			Diagnostic.Message.Contains(PublicActionId.ToString())
		)
		{
			OutError = Diagnostic.Code.ToString();
			return false;
		}
	}
	OutDefinition = DefinitionIndex.FindDefinition(PublicActionId);
	if (!OutDefinition)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLISHED_PUBLIC_ACTION_REQUIRED");
		return false;
	}
	return true;
}

bool HaveSameNames(const TArray<FName>& A, const TArray<FName>& B)
{
	if (A.Num() != B.Num())
	{
		return false;
	}
	TArray<FName> SortedA = A;
	TArray<FName> SortedB = B;
	SortedA.Sort(FNameLexicalLess());
	SortedB.Sort(FNameLexicalLess());
	return SortedA == SortedB;
}

bool ValidateTemplateVocabularyTags(
	const ULLMNPCMotionTemplate& Template,
	const ULLMNPCActionVocabulary& Vocabulary,
	FString& OutError
)
{
	const FLLMNPCTemplateMetadata& Metadata = Template.Metadata;
	return
		Vocabulary.ValidateTags(
			Metadata.IntentTags,
			ELLMNPCActionVocabularyField::Intent,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.EmotionTags,
			ELLMNPCActionVocabularyField::Emotion,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.PersonalityTags,
			ELLMNPCActionVocabularyField::Personality,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.BodyRegionTags,
			ELLMNPCActionVocabularyField::BodyRegion,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.SpatialRequirementTags,
			ELLMNPCActionVocabularyField::SpatialRequirement,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.SemanticEffectTags,
			ELLMNPCActionVocabularyField::SemanticEffect,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.TargetCategoryTags,
			ELLMNPCActionVocabularyField::TargetCategory,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.VariantStyleTags,
			ELLMNPCActionVocabularyField::VariantStyle,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Template.ModifierPolicy.AllowedStyleTags,
			ELLMNPCActionVocabularyField::VariantStyle,
			OutError
		);
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

bool IsFixedOneRange(const FVector2D& Range)
{
	return
		FMath::IsNearlyEqual(Range.X, 1.0f) &&
		FMath::IsNearlyEqual(Range.Y, 1.0f);
}

bool HasSafeAnimationAssetModifierPolicy(
	const FLLMNPCModifierPolicy& Policy
)
{
	return
		IsFixedOneRange(Policy.AmplitudeRange) &&
		!Policy.bAllowMirror &&
		!Policy.bEnableDynamicTargetTracking &&
		!Policy.bEnableObstacleAdaptation &&
		FMath::IsNearlyZero(Policy.RandomAmplitudeJitter) &&
		FMath::IsNearlyZero(Policy.RandomFrequencyJitter) &&
		FMath::IsNearlyZero(Policy.RandomPhaseJitterRadians);
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

FLLMNPCAuthoringOperationResult
ULLMNPCTemplateAuthoringSubsystem::ImportAnimationDraftFromFile(
	const FString& DraftFilePath,
	UAnimationAsset* SelectedAnimationAsset,
	const FString& DestinationPackagePath
)
{
	FString DraftJson;
	if (!FFileHelper::LoadFileToString(DraftJson, *DraftFilePath))
	{
		return ErrorResult(
			TEXT("LLMNPC_ANIMATION_DRAFT_FILE_READ_FAILED"),
			TEXT("Could not read the Animation Template Draft JSON file.")
		);
	}
	return ImportAnimationDraftJson(
		DraftJson,
		SelectedAnimationAsset,
		DestinationPackagePath
	);
}

FLLMNPCAuthoringOperationResult
ULLMNPCTemplateAuthoringSubsystem::ImportAnimationDraftJson(
	const FString& DraftJson,
	UAnimationAsset* SelectedAnimationAsset,
	const FString& DestinationPackagePath
)
{
	if (!SelectedAnimationAsset)
	{
		return ErrorResult(
			TEXT("LLMNPC_ANIMATION_DRAFT_ASSET_SELECTION_REQUIRED"),
			TEXT("Select an Animation Asset in the Workbench Asset Picker.")
		);
	}

	FString DirectoryError;
	if (!EnsureAuthoringDirectories(DirectoryError))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_DIRECTORY_FAILED"),
			DirectoryError
		);
	}

	ULLMNPCMotionTemplate* ParsedTemplate =
		NewObject<ULLMNPCMotionTemplate>(GetTransientPackage());
	FLLMNPCParsedAnimationDraftInfo ParsedInfo;
	FString Error;
	if (!FLLMNPCAnimationTemplateDraftImporter::ParseDraftJson(
		DraftJson,
		*SelectedAnimationAsset,
		*ParsedTemplate,
		ParsedInfo,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}

	ULLMNPCSkeletonProfile* SkeletonProfile =
		FindSkeletonProfile(ParsedTemplate->Metadata.SkeletonProfileId);
	FString ProfileError;
	if (
		!SkeletonProfile ||
		!SkeletonProfile->ValidateProfile(ProfileError)
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_ANIMATION_DRAFT_SKELETON_PROFILE_INVALID"),
			ProfileError.IsEmpty()
				? TEXT("The selected Skeleton Profile was not found.")
				: ProfileError
		);
	}
	if (!SkeletonProfile->IsCompatibleSkeleton(
		SelectedAnimationAsset->GetSkeleton()
	))
	{
		return ErrorResult(
			TEXT("LLMNPC_ANIMATION_DRAFT_SKELETON_INCOMPATIBLE"),
			TEXT("The selected Animation Asset is not compatible with the Draft Skeleton Profile.")
		);
	}

	const FString DraftHash =
		FLLMNPCUEPIArtifactAdapter::HashJson(DraftJson);
	FString SafeHash = DraftHash;
	SafeHash.ReplaceInline(TEXT(":"), TEXT("_"));
	const FString SourceCopyPath = FPaths::Combine(
		GetDraftDirectory(),
		FString::Printf(
			TEXT("%s_%s.json"),
			*ParsedInfo.AssetName,
			*SafeHash.Right(12)
		)
	);
	if (!FFileHelper::SaveStringToFile(
		DraftJson,
		*SourceCopyPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		return ErrorResult(
			TEXT("LLMNPC_ANIMATION_DRAFT_SOURCE_COPY_FAILED"),
			TEXT("Could not preserve the imported Animation Template Draft source.")
		);
	}

	TSharedPtr<FJsonObject> Provenance;
	if (!ParseJsonObject(
		ParsedTemplate->SourceProvenanceJson,
		Provenance
	))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PROVENANCE_JSON_INVALID"),
			TEXT("Animation Draft provenance could not be parsed after validation.")
		);
	}
	TSharedRef<FJsonObject> ImportRecord = MakeShared<FJsonObject>();
	ImportRecord->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.animation_draft_import.v1")
	);
	ImportRecord->SetStringField(
		TEXT("draft_content_hash"),
		DraftHash
	);
	ImportRecord->SetStringField(
		TEXT("draft_source_copy_path"),
		FPaths::ConvertRelativePathToFull(SourceCopyPath)
	);
	ImportRecord->SetStringField(
		TEXT("selected_asset_path"),
		SelectedAnimationAsset->GetPathName()
	);
	ImportRecord->SetStringField(
		TEXT("imported_at_utc"),
		FDateTime::UtcNow().ToIso8601()
	);
	Provenance->SetObjectField(TEXT("import_record"), ImportRecord);
	if (!SerializeJsonObject(
		Provenance.ToSharedRef(),
		ParsedTemplate->SourceProvenanceJson
	))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PROVENANCE_SERIALIZE_FAILED"),
			TEXT("Animation Draft provenance could not be updated with the import record.")
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
		TEXT("Animation Draft imported as a Generated template. The asset path came from the UE Asset Picker and is not runtime-selectable yet."),
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

	const bool bAnimationTemplate =
		Template->Kind == ELLMNPCTemplateKind::AnimationAsset;
	if (!bAnimationTemplate)
	{
		FLLMMotionPlan Plan;
		Plan.Intent = Template->Metadata.PublicActionId.ToString();
		Plan.Clip = Template->ProceduralClip;
		ULLMNPCMotionValidator* Validator =
			NewObject<ULLMNPCMotionValidator>(this);
		const FLLMMotionValidationResult MotionResult =
			Validator->ValidateAndClamp(
				Plan,
				ELLMNPCMotionValidationSource::PublishedTemplate
			);
		AddCheck(
			TEXT("motion_validator"),
			MotionResult.bValid,
			MotionResult.bValid
				? TEXT("Motion tracks pass the Published Template trust boundary.")
				: MotionResult.ErrorMessage
		);
	}

	ULLMNPCSkeletonProfile* SkeletonProfile = FindSkeletonProfile(Template->Metadata.SkeletonProfileId);
	FString ProfileValidationError;
	const bool bProfileValid = SkeletonProfile && SkeletonProfile->ValidateProfile(ProfileValidationError);
	AddCheck(
		TEXT("skeleton_profile"),
		bProfileValid,
		bProfileValid ? TEXT("Skeleton Profile exists and validates.") :
			(ProfileValidationError.IsEmpty() ? TEXT("Skeleton Profile was not found.") : ProfileValidationError)
	);

	UAnimSequenceBase* AnimationSequence = nullptr;
	FString CurrentAnimationAssetPackageHash;
	FString AnimationAssetPackageHashError;
	if (bAnimationTemplate)
	{
		UAnimationAsset* AnimationAsset =
			Template->AnimationAsset.LoadSynchronous();
		AddCheck(
			TEXT("animation_asset_load"),
			AnimationAsset != nullptr,
			AnimationAsset
				? FString::Printf(
					TEXT("Animation Asset loaded: %s"),
					*AnimationAsset->GetPathName()
				)
				: TEXT("Animation Asset could not be loaded.")
		);

		AnimationSequence = Cast<UAnimSequenceBase>(AnimationAsset);
		AddCheck(
			TEXT("animation_asset_type"),
			AnimationSequence != nullptr,
			AnimationSequence
				? TEXT("Animation Asset is a supported sequence or montage.")
				: TEXT("Animation Asset is not a supported UAnimSequenceBase.")
		);
		const bool bPackageHashAvailable =
			AnimationAsset &&
			FLLMNPCAnimationTemplateDraftImporter::
				BuildAnimationAssetPackageHash(
					*AnimationAsset,
					CurrentAnimationAssetPackageHash,
					AnimationAssetPackageHashError
				);
		AddCheck(
			TEXT("animation_asset_saved_package"),
			bPackageHashAvailable,
			bPackageHashAvailable
				? TEXT("Animation Asset is saved and its source package fingerprint is available.")
				: (
					AnimationAssetPackageHashError.IsEmpty()
						? TEXT("Animation Asset source package could not be fingerprinted.")
						: AnimationAssetPackageHashError
				)
		);

		const USkeleton* AnimationSkeleton =
			AnimationAsset ? AnimationAsset->GetSkeleton() : nullptr;
		const bool bSkeletonCompatible =
			bProfileValid &&
			AnimationSkeleton &&
			SkeletonProfile->IsCompatibleSkeleton(AnimationSkeleton);
		AddCheck(
			TEXT("animation_skeleton_compatibility"),
			bSkeletonCompatible,
			bSkeletonCompatible
				? TEXT("Animation Asset Skeleton matches the selected Manny Profile.")
				: TEXT("Animation Asset Skeleton is not compatible with the selected Skeleton Profile.")
		);

		bool bSlotValid =
			AnimationSkeleton &&
			AnimationSkeleton->ContainsSlotName(
				Template->AnimationPlayback.SlotName
			);
		if (
			bSlotValid &&
			Cast<UAnimMontage>(AnimationAsset)
		)
		{
			bSlotValid = false;
			const UAnimMontage* Montage =
				CastChecked<UAnimMontage>(AnimationAsset);
			for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
			{
				if (
					SlotTrack.SlotName ==
					Template->AnimationPlayback.SlotName
				)
				{
					bSlotValid = true;
					break;
				}
			}
		}
		AddCheck(
			TEXT("animation_slot"),
			bSlotValid,
			bSlotValid
				? FString::Printf(
					TEXT("Slot %s is registered and usable."),
					*Template->AnimationPlayback.SlotName.ToString()
				)
				: FString::Printf(
					TEXT("Slot %s is not registered for this asset."),
					*Template->AnimationPlayback.SlotName.ToString()
				)
		);

		const bool bRootMotionSafe =
			AnimationSequence &&
			!AnimationSequence->HasRootMotion() &&
			!Template->AnimationPlayback.bAllowRootMotion;
		AddCheck(
			TEXT("animation_root_motion"),
			bRootMotionSafe,
			bRootMotionSafe
				? TEXT("Root Motion is absent and forbidden by policy.")
				: TEXT("v0.10 Animation Asset templates cannot contain or enable Root Motion.")
		);

		const bool bAssetModifierPolicySafe =
			HasSafeAnimationAssetModifierPolicy(
				Template->ModifierPolicy
			);
		AddCheck(
			TEXT("animation_modifier_surface"),
			bAssetModifierPolicySafe,
			bAssetModifierPolicySafe
				? TEXT("Only bounded speed and duration modifiers are exposed; amplitude and procedural dimensions are fixed.")
				: TEXT("Animation Asset templates must fix amplitude, mirror, and procedural modifier dimensions.")
		);

		const float RemainingAnimationSeconds =
			AnimationSequence
				? AnimationSequence->GetPlayLength() -
					Template->AnimationPlayback.StartPositionSeconds
				: 0.0f;
		const float SlowestPlayRate =
			Template->ModifierPolicy.SpeedRange.X /
			FMath::Max(
				Template->ModifierPolicy.DurationRange.Y,
				KINDA_SMALL_NUMBER
			);
		const float LongestPlaybackSeconds =
			RemainingAnimationSeconds /
			FMath::Max(SlowestPlayRate, KINDA_SMALL_NUMBER);
		const bool bDurationSafe =
			AnimationSequence &&
			RemainingAnimationSeconds > 0.0f &&
			Template->AnimationPlayback.MaxDurationSeconds + KINDA_SMALL_NUMBER >=
				LongestPlaybackSeconds;
		AddCheck(
			TEXT("animation_duration"),
			bDurationSafe,
			bDurationSafe
				? FString::Printf(
					TEXT("Max Duration covers the slowest permitted playback (%.3fs)."),
					LongestPlaybackSeconds
				)
				: FString::Printf(
					TEXT("Max Duration would truncate the slowest permitted playback (needs %.3fs)."),
					LongestPlaybackSeconds
				)
		);

		const bool bLoopInterruptSafe =
			!Template->AnimationPlayback.bLoop ||
			Template->AnimationPlayback.bInterruptible;
		AddCheck(
			TEXT("animation_loop_interrupt"),
			bLoopInterruptSafe,
			bLoopInterruptSafe
				? TEXT("Loop and interruption policy has a bounded recovery path.")
				: TEXT("A looping Animation Asset must be interruptible.")
		);

		const TArray<FName>& Channels =
			Template->Metadata.RequiredChannels;
		const bool bChannelContractSafe =
			!Channels.IsEmpty() &&
			!(
				Channels.Contains(TEXT("full_body")) &&
				Channels.Num() > 1
			);
		AddCheck(
			TEXT("animation_channel_contract"),
			bChannelContractSafe,
			bChannelContractSafe
				? TEXT("Animation Asset declares an explicit non-ambiguous body-region Channel contract.")
				: TEXT("Animation Asset Channels are missing or mix full_body with regional Channels.")
		);
	}

	TSharedPtr<FJsonObject> Provenance;
	const bool bProvenanceJsonValid = ParseJsonObject(Template->SourceProvenanceJson, Provenance);
	AddCheck(
		TEXT("provenance_json"),
		bProvenanceJsonValid,
		bProvenanceJsonValid ? TEXT("Provenance JSON parses.") : TEXT("Provenance JSON is invalid.")
	);
	if (bAnimationTemplate)
	{
		FString ImportedAnimationAssetPackageHash;
		const bool bPackageHashMatches =
			bProvenanceJsonValid &&
			Provenance->TryGetStringField(
				TEXT("source_asset_package_hash"),
				ImportedAnimationAssetPackageHash
			) &&
			!CurrentAnimationAssetPackageHash.IsEmpty() &&
			ImportedAnimationAssetPackageHash ==
				CurrentAnimationAssetPackageHash;
		AddCheck(
			TEXT("animation_asset_source_fingerprint"),
			bPackageHashMatches,
			bPackageHashMatches
				? TEXT("The selected Animation Asset still matches the package imported into this Draft.")
				: TEXT("The selected Animation Asset has changed since import; re-import before review.")
		);
	}

	FLLMNPCUEPIReconstructionSummary Summary;
	FString AuthoringContext;
	FString ArtifactError;
	bool bArtifactValid = false;
	const bool bReconstructionRequested =
		!ReconstructionProfileFilePath.TrimStartAndEnd().IsEmpty();
	if (bReconstructionRequested)
	{
		bArtifactValid = FLLMNPCUEPIArtifactAdapter::LoadReconstructionProfile(
			ReconstructionProfileFilePath,
			Summary,
			AuthoringContext,
			ArtifactError
		);
	}
	FString ExpectedReconstructionHash;
	if (bProvenanceJsonValid)
	{
		Provenance->TryGetStringField(TEXT("reconstruction_profile_hash"), ExpectedReconstructionHash);
	}
	ExpectedReconstructionHash =
		ExpectedReconstructionHash.TrimStartAndEnd();
	if (bAnimationTemplate)
	{
		if (bReconstructionRequested)
		{
			AddCheck(
				TEXT("reconstruction_profile"),
				bArtifactValid,
				bArtifactValid
					? TEXT("Optional UEPI Reconstruction Profile validates.")
					: (
						ArtifactError.IsEmpty()
							? TEXT("The optional Reconstruction Profile could not be read.")
							: ArtifactError
					)
			);
			if (!ExpectedReconstructionHash.IsEmpty())
			{
				const bool bArtifactHashMatches =
					bArtifactValid &&
					ExpectedReconstructionHash ==
						Summary.ProfileContentHash;
				AddCheck(
					TEXT("reconstruction_hash"),
					bArtifactHashMatches,
					bArtifactHashMatches
						? TEXT("Draft provenance matches the optional Reconstruction Profile hash.")
						: TEXT("Draft provenance does not match the supplied Reconstruction Profile hash.")
				);
			}
			else if (bArtifactValid)
			{
				Warnings.Add(MakeShared<FJsonValueString>(
					TEXT("Optional Reconstruction evidence was checked but the Draft did not pin its hash.")
				));
			}
		}
		else if (!ExpectedReconstructionHash.IsEmpty())
		{
			AddCheck(
				TEXT("reconstruction_profile"),
				false,
				TEXT("Draft provenance pins Reconstruction evidence, so that evidence must be supplied.")
			);
		}
		else
		{
			Warnings.Add(MakeShared<FJsonValueString>(
				TEXT("No optional Reconstruction evidence was supplied for this Animation Asset wrapper.")
			));
		}
	}
	else
	{
		AddCheck(
			TEXT("reconstruction_profile"),
			bArtifactValid,
			bArtifactValid
				? TEXT("UEPI Reconstruction Profile validates.")
				: (
					ArtifactError.IsEmpty()
						? TEXT("A Reconstruction Profile is required.")
						: ArtifactError
				)
		);
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
	}

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

	if (bArtifactValid && !bAnimationTemplate)
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
	Report->SetStringField(
		TEXT("template_kind"),
		bAnimationTemplate
			? TEXT("animation_asset")
			: TEXT("procedural_motion")
	);
	Report->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());
	Report->SetStringField(TEXT("source_reconstruction_hash"), Summary.ProfileContentHash);
	Report->SetStringField(TEXT("full_pose_validation"), FullPoseValidation);
	if (bAnimationTemplate)
	{
		Report->SetStringField(
			TEXT("source_asset_path"),
			Template->AnimationAsset.ToSoftObjectPath().ToString()
		);
		Report->SetStringField(
			TEXT("source_asset_skeleton"),
			AnimationSequence && AnimationSequence->GetSkeleton()
				? AnimationSequence->GetSkeleton()->GetPathName()
				: FString()
		);
		Report->SetNumberField(
			TEXT("source_asset_play_length_seconds"),
			AnimationSequence
				? AnimationSequence->GetPlayLength()
				: 0.0f
		);
		Report->SetStringField(
			TEXT("source_asset_package_hash"),
			CurrentAnimationAssetPackageHash
		);
	}
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
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const FString ResolvedDestination = DestinationPackagePath.TrimStartAndEnd().IsEmpty()
		? (Settings
			? Settings->ProjectPublishedTemplatePath
			: TEXT("/Game/LLMNPCActionLayer/MotionTemplates/Published"))
		: DestinationPackagePath.TrimStartAndEnd();
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
	PublishCandidate->Metadata.CatalogContentHash.Reset();
	PublishCandidate->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(*PublishCandidate);
	FString ValidationError;
	if (!PublishCandidate->ValidateTemplate(ValidationError))
	{
		return ErrorResult(FName(*ValidationError), ValidationError);
	}

	ULLMNPCMotionTemplate* PublishedAsset = nullptr;
	if (!CreateTemplateAsset(
		ResolvedDestination,
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
	FString SourcePath;
	if (!ExportPublishedTemplateSource(*PublishedAsset, SourcePath, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}

	UAssetManager::Get().ScanPathForPrimaryAssets(
		TEXT("LLMNPCTemplate"),
		ResolvedDestination,
		ULLMNPCMotionTemplate::StaticClass(),
		false
	);
	return SuccessResult(
		FString::Printf(
			TEXT("HumanApproved template published with canonical source: %s"),
			*SourcePath
		),
		PublishedAsset->GetPathName(),
		PublishedAsset
	);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::MarkPublicActionPreviewed(
	ULLMNPCPublicActionDefinition* Definition,
	const FString& PreviewNotes
)
{
	if (
		!Definition ||
		(Definition->ReviewState != ELLMNPCTemplateReviewState::Draft &&
			Definition->ReviewState != ELLMNPCTemplateReviewState::Generated)
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_PREVIEW_STATE_INVALID"),
			TEXT("Only a Draft or Generated Public Action can be marked Previewed.")
		);
	}
	const FString Notes = PreviewNotes.TrimStartAndEnd();
	if (Notes.IsEmpty())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_PREVIEW_NOTES_REQUIRED"),
			TEXT("Preview notes are required.")
		);
	}
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	ULLMNPCActionVocabulary* Vocabulary =
		Settings ? Settings->ActionVocabulary.LoadSynchronous() : nullptr;
	FString Error;
	Definition->ContentHash.Reset();
	Definition->ContentHash =
		ULLMNPCPublicActionDefinition::BuildContentHash(*Definition);
	if (!Definition->ValidateDefinition(Vocabulary, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	if (!AppendPublicActionReviewRecord(
		*Definition,
		TEXT("previewed"),
		TEXT("preview_operator"),
		Notes,
		false,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}
	Definition->ReviewState = ELLMNPCTemplateReviewState::Previewed;
	if (!SavePublicActionAsset(Definition, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	return PublicActionSuccessResult(
		TEXT("Public Action marked Previewed; human approval is still required."),
		Definition->GetPathName(),
		Definition
	);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::ApprovePublicAction(
	ULLMNPCPublicActionDefinition* Definition,
	const FString& Reviewer,
	const FString& ReviewNotes
)
{
	if (!Definition || Definition->ReviewState != ELLMNPCTemplateReviewState::Previewed)
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_APPROVAL_STATE_INVALID"),
			TEXT("Only a Previewed Public Action can be approved.")
		);
	}
	const FString CleanReviewer = Reviewer.TrimStartAndEnd();
	const FString CleanNotes = ReviewNotes.TrimStartAndEnd();
	if (CleanReviewer.IsEmpty() || CleanNotes.IsEmpty())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_APPROVAL_IDENTITY_REQUIRED"),
			TEXT("Reviewer identity and review notes are required.")
		);
	}
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	ULLMNPCActionVocabulary* Vocabulary =
		Settings ? Settings->ActionVocabulary.LoadSynchronous() : nullptr;
	FString Error;
	if (
		!Definition->ValidateDefinition(Vocabulary, Error) ||
		!AppendPublicActionReviewRecord(
			*Definition,
			TEXT("human_approved"),
			CleanReviewer,
			CleanNotes,
			true,
			Error
		)
	)
	{
		return ErrorResult(FName(*Error), Error);
	}
	Definition->ReviewState = ELLMNPCTemplateReviewState::HumanApproved;
	if (!SavePublicActionAsset(Definition, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	return PublicActionSuccessResult(
		TEXT("Public Action is HumanApproved and eligible for Publish."),
		Definition->GetPathName(),
		Definition
	);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::RejectPublicAction(
	ULLMNPCPublicActionDefinition* Definition,
	const FString& Reviewer,
	const FString& RejectionReason
)
{
	if (!Definition || Definition->IsPublished())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_REJECT_STATE_INVALID"),
			TEXT("A Published Public Action cannot be rejected in place.")
		);
	}
	const FString CleanReviewer = Reviewer.TrimStartAndEnd();
	const FString CleanReason = RejectionReason.TrimStartAndEnd();
	if (CleanReviewer.IsEmpty() || CleanReason.IsEmpty())
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_REJECTION_REASON_REQUIRED"),
			TEXT("Reviewer identity and rejection reason are required.")
		);
	}
	FString Error;
	if (!AppendPublicActionReviewRecord(
		*Definition,
		TEXT("rejected"),
		CleanReviewer,
		CleanReason,
		false,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}
	Definition->ReviewState = ELLMNPCTemplateReviewState::Rejected;
	if (!SavePublicActionAsset(Definition, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	return PublicActionSuccessResult(
		TEXT("Public Action rejected and excluded from the runtime catalog."),
		Definition->GetPathName(),
		Definition
	);
}

FLLMNPCAuthoringOperationResult ULLMNPCTemplateAuthoringSubsystem::PublishPublicAction(
	ULLMNPCPublicActionDefinition* Definition,
	const FString& DestinationPackagePath
)
{
	FString Error;
	if (!CanPublishPublicAction(Definition, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	if (HasPublishedPublicActionVersion(*Definition, Definition))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_VERSION_ALREADY_PUBLISHED"),
			TEXT("This Public Action semantic version and definition revision are already Published.")
		);
	}

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const FString ResolvedDestination = DestinationPackagePath.TrimStartAndEnd().IsEmpty()
		? (Settings
			? Settings->ProjectPublishedPublicActionPath
			: TEXT("/Game/LLMNPCActionLayer/PublicActions/Published"))
		: DestinationPackagePath.TrimStartAndEnd();
	FString PublishedAssetName = Definition->GetName();
	if (!PublishedAssetName.StartsWith(TEXT("PA_")))
	{
		PublishedAssetName = TEXT("PA_") + PublishedAssetName;
	}
	ULLMNPCPublicActionDefinition* PublishCandidate =
		DuplicateObject<ULLMNPCPublicActionDefinition>(
			Definition,
			GetTransientPackage()
		);
	PublishCandidate->ReviewState = ELLMNPCTemplateReviewState::Published;
	PublishCandidate->ContentHash.Reset();
	PublishCandidate->ContentHash =
		ULLMNPCPublicActionDefinition::BuildContentHash(*PublishCandidate);

	ULLMNPCActionVocabulary* Vocabulary =
		Settings ? Settings->ActionVocabulary.LoadSynchronous() : nullptr;
	if (!PublishCandidate->ValidateDefinition(Vocabulary, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	ULLMNPCPublicActionDefinition* PublishedAsset = nullptr;
	if (!CreatePublicActionAsset(
		ResolvedDestination,
		PublishedAssetName,
		*PublishCandidate,
		PublishedAsset,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}
	if (!SavePublicActionAsset(PublishedAsset, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	FString SourcePath;
	if (!ExportPublishedPublicActionSource(*PublishedAsset, SourcePath, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}
	UAssetManager::Get().ScanPathForPrimaryAssets(
		TEXT("LLMNPCPublicAction"),
		ResolvedDestination,
		ULLMNPCPublicActionDefinition::StaticClass(),
		false
	);
	return PublicActionSuccessResult(
		FString::Printf(
			TEXT("Public Action published with canonical source: %s"),
			*SourcePath
		),
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
	if (Template->Kind == ELLMNPCTemplateKind::AnimationAsset)
	{
		FString QualityError;
		if (!HasCurrentPassingQualityReport(*Template, QualityError))
		{
			return ErrorResult(FName(*QualityError), QualityError);
		}
		if (!MotionComponent->PreviewAnimationAssetTemplate(
			*Template,
			FLLMNPCTemplateModifiers()
		))
		{
			return ErrorResult(
				TEXT("LLMNPC_AUTHORING_PREVIEW_SUBMIT_FAILED"),
				MotionComponent->LastValidationError
			);
		}
		return SuccessResult(
			TEXT("Animation Asset Draft submitted to the PIE preview actor through Dynamic Montage."),
			PreviewActor->GetPathName(),
			Template
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
	return
		ValidateProvenanceForPublish(*Template, OutError) &&
		ValidateTemplateCatalogForPublish(*Template, OutError);
}

bool ULLMNPCTemplateAuthoringSubsystem::CanPublishPublicAction(
	const ULLMNPCPublicActionDefinition* Definition,
	FString& OutError
) const
{
	OutError.Reset();
	if (
		!Definition ||
		Definition->ReviewState != ELLMNPCTemplateReviewState::HumanApproved
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_HUMAN_APPROVAL_REQUIRED");
		return false;
	}
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	ULLMNPCActionVocabulary* Vocabulary =
		Settings ? Settings->ActionVocabulary.LoadSynchronous() : nullptr;
	if (!Vocabulary)
	{
		OutError = TEXT("LLMNPC_AUTHORING_VOCABULARY_MISSING");
		return false;
	}
	if (!Definition->ValidateDefinition(Vocabulary, OutError))
	{
		return false;
	}
	TSharedPtr<FJsonObject> ReviewRecord;
	const TSharedPtr<FJsonObject>* HumanReview = nullptr;
	bool bApproved = false;
	if (
		!ParseJsonObject(Definition->ReviewRecordJson, ReviewRecord) ||
		!ReviewRecord->TryGetObjectField(TEXT("human_review"), HumanReview) ||
		!HumanReview ||
		!HumanReview->IsValid() ||
		!(*HumanReview)->TryGetBoolField(TEXT("approved"), bApproved) ||
		!bApproved
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_REVIEW_REQUIRED");
		return false;
	}
	return true;
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

FString ULLMNPCTemplateAuthoringSubsystem::GetPublishedSourceDirectory()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	if (Plugin.IsValid() && !Plugin->GetDescriptor().bInstalled)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources/Templates/Published")
		));
	}
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("LLMNPCSource/Templates/Published")
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
		FPaths::Combine(FPaths::GetPath(GetDraftDirectory()), TEXT("Contexts")),
		GetPublishedSourceDirectory(),
		FPaths::ConvertRelativePathToFull(FPaths::Combine(
			FPaths::ProjectDir(),
			TEXT("LLMNPCSource/PublicActions/Published")
		))
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

bool ULLMNPCTemplateAuthoringSubsystem::SavePublicActionAsset(
	ULLMNPCPublicActionDefinition* Definition,
	FString& OutError
)
{
	OutError.Reset();
	if (!Definition)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_MISSING");
		return false;
	}
	UPackage* Package = Definition->GetOutermost();
	if (!Package || Package == GetTransientPackage())
	{
		return true;
	}
	Package->MarkPackageDirty();
	const FString PackageName = Package->GetName();
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_PACKAGE_NAME_INVALID");
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
	if (!UPackage::SavePackage(Package, Definition, *Filename, SaveArgs))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_ASSET_SAVE_FAILED");
		return false;
	}
	return true;
}

bool ULLMNPCTemplateAuthoringSubsystem::ExportPublishedTemplateSource(
	const ULLMNPCMotionTemplate& Template,
	FString& OutPath,
	FString& OutError
)
{
	OutPath.Reset();
	if (!Template.IsPublished())
	{
		OutError = TEXT("LLMNPC_AUTHORING_SOURCE_REQUIRES_PUBLISHED_TEMPLATE");
		return false;
	}
	FString MetadataJson;
	FString ModifierJson;
	FString ClipJson;
	FString PlaybackJson;
	if (
		!FJsonObjectConverter::UStructToJsonObjectString(Template.Metadata, MetadataJson) ||
		!FJsonObjectConverter::UStructToJsonObjectString(Template.ModifierPolicy, ModifierJson) ||
		!FJsonObjectConverter::UStructToJsonObjectString(Template.ProceduralClip, ClipJson) ||
		!FJsonObjectConverter::UStructToJsonObjectString(Template.AnimationPlayback, PlaybackJson)
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_SOURCE_STRUCT_SERIALIZE_FAILED");
		return false;
	}
	TSharedPtr<FJsonObject> MetadataObject;
	TSharedPtr<FJsonObject> ModifierObject;
	TSharedPtr<FJsonObject> ClipObject;
	TSharedPtr<FJsonObject> PlaybackObject;
	if (
		!ParseJsonObject(MetadataJson, MetadataObject) ||
		!ParseJsonObject(ModifierJson, ModifierObject) ||
		!ParseJsonObject(ClipJson, ClipObject) ||
		!ParseJsonObject(PlaybackJson, PlaybackObject)
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_SOURCE_STRUCT_JSON_INVALID");
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.published_template_source.v1")
	);
	Root->SetStringField(TEXT("asset_path"), Template.GetPathName());
	Root->SetStringField(TEXT("catalog_content_hash"), Template.Metadata.CatalogContentHash);
	Root->SetNumberField(TEXT("template_kind"), static_cast<int32>(Template.Kind));
	Root->SetObjectField(TEXT("metadata"), MetadataObject.ToSharedRef());
	Root->SetObjectField(TEXT("modifier_policy"), ModifierObject.ToSharedRef());
	Root->SetObjectField(TEXT("procedural_clip"), ClipObject.ToSharedRef());
	Root->SetStringField(
		TEXT("animation_asset"),
		Template.AnimationAsset.ToSoftObjectPath().ToString()
	);
	Root->SetObjectField(TEXT("animation_playback"), PlaybackObject.ToSharedRef());
	TSharedPtr<FJsonObject> Provenance;
	if (ParseJsonObject(Template.SourceProvenanceJson, Provenance))
	{
		const TSharedPtr<FJsonObject>* ImportRecord = nullptr;
		if (
			Provenance->TryGetObjectField(TEXT("import_record"), ImportRecord) &&
			ImportRecord && ImportRecord->IsValid()
		)
		{
			(*ImportRecord)->RemoveField(TEXT("draft_source_copy_path"));
		}
		Root->SetObjectField(TEXT("source_provenance"), Provenance.ToSharedRef());
	}
	TSharedPtr<FJsonObject> QualityReport;
	if (ParseJsonObject(Template.ValidationReportJson, QualityReport))
	{
		Root->SetObjectField(TEXT("quality_report"), QualityReport.ToSharedRef());
	}

	FString SourceJson;
	if (!SerializeJsonObject(Root, SourceJson))
	{
		OutError = TEXT("LLMNPC_AUTHORING_SOURCE_SERIALIZE_FAILED");
		return false;
	}
	FString DirectoryError;
	if (!EnsureAuthoringDirectories(DirectoryError))
	{
		OutError = DirectoryError;
		return false;
	}
	FString FileStem = FString::Printf(
		TEXT("%s_%s_r%d"),
		*Template.Metadata.TemplateId.ToString(),
		*Template.Metadata.SemanticVersion,
		Template.Metadata.CatalogRevision
	);
	FileStem.ReplaceInline(TEXT("."), TEXT("_"));
	OutPath = FPaths::Combine(
		GetPublishedSourceDirectory(),
		FileStem + TEXT(".json")
	);
	if (!FFileHelper::SaveStringToFile(
		SourceJson,
		*OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		OutError = TEXT("LLMNPC_AUTHORING_SOURCE_WRITE_FAILED");
		OutPath.Reset();
		return false;
	}
	return true;
}

bool ULLMNPCTemplateAuthoringSubsystem::ExportPublishedPublicActionSource(
	const ULLMNPCPublicActionDefinition& Definition,
	FString& OutPath,
	FString& OutError
)
{
	OutPath.Reset();
	if (!Definition.IsPublished())
	{
		OutError = TEXT("LLMNPC_AUTHORING_SOURCE_REQUIRES_PUBLISHED_PUBLIC_ACTION");
		return false;
	}
	FString ObjectJson;
	if (!FJsonObjectConverter::UStructToJsonObjectString(
		Definition.GetClass(),
		&Definition,
		ObjectJson,
		0,
		0
	))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_SOURCE_SERIALIZE_FAILED");
		return false;
	}
	TSharedPtr<FJsonObject> DefinitionObject;
	if (!ParseJsonObject(ObjectJson, DefinitionObject))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_SOURCE_JSON_INVALID");
		return false;
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.published_public_action_source.v1")
	);
	Root->SetStringField(TEXT("asset_path"), Definition.GetPathName());
	Root->SetStringField(TEXT("content_hash"), Definition.ContentHash);
	Root->SetObjectField(TEXT("definition"), DefinitionObject.ToSharedRef());
	FString SourceJson;
	if (!SerializeJsonObject(Root, SourceJson))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_SOURCE_SERIALIZE_FAILED");
		return false;
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("LLMNPCSource/PublicActions/Published")
	));
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_SOURCE_DIRECTORY_FAILED");
		return false;
	}
	FString FileStem = FString::Printf(
		TEXT("%s_%s_r%d"),
		*Definition.PublicActionId.ToString(),
		*Definition.SemanticVersion,
		Definition.DefinitionRevision
	);
	FileStem.ReplaceInline(TEXT("."), TEXT("_"));
	OutPath = FPaths::Combine(Directory, FileStem + TEXT(".json"));
	if (!FFileHelper::SaveStringToFile(
		SourceJson,
		*OutPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_SOURCE_WRITE_FAILED");
		OutPath.Reset();
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
	FString AnimationPlaybackJson;
	FString StableProvenanceJson = Template.SourceProvenanceJson;
	FLLMNPCTemplateMetadata StableMetadata = Template.Metadata;
	StableMetadata.ReviewState = ELLMNPCTemplateReviewState::Generated;
	FJsonObjectConverter::UStructToJsonObjectString(StableMetadata, MetadataJson);
	FJsonObjectConverter::UStructToJsonObjectString(Template.ModifierPolicy, ModifierJson);
	FJsonObjectConverter::UStructToJsonObjectString(Template.ProceduralClip, ClipJson);
	FJsonObjectConverter::UStructToJsonObjectString(Template.AnimationPlayback, AnimationPlaybackJson);
	TSharedPtr<FJsonObject> StableProvenance;
	if (ParseJsonObject(Template.SourceProvenanceJson, StableProvenance))
	{
		StableProvenance->RemoveField(TEXT("review_history"));
		StableProvenance->RemoveField(TEXT("human_review"));
		SerializeJsonObject(StableProvenance.ToSharedRef(), StableProvenanceJson);
	}
	const FString StableContent = Template.Kind == ELLMNPCTemplateKind::AnimationAsset
		? FString::Printf(
			TEXT("%d|%s|%s|%s|%s|%s|%s"),
			static_cast<int32>(Template.Kind),
			*MetadataJson,
			*ModifierJson,
			*ClipJson,
			*Template.AnimationAsset.ToSoftObjectPath().ToString(),
			*AnimationPlaybackJson,
			*StableProvenanceJson
		)
		: FString::Printf(
			TEXT("%d|%s|%s|%s|%s"),
			static_cast<int32>(Template.Kind),
			*MetadataJson,
			*ModifierJson,
			*ClipJson,
			*StableProvenanceJson
		);
	return FLLMNPCUEPIArtifactAdapter::HashJson(StableContent);
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
	if (Template.Kind == ELLMNPCTemplateKind::AnimationAsset)
	{
		FString ReportAssetPath;
		FString ReportSkeletonPath;
		FString ReportPackageHash;
		FString CurrentPackageHash;
		FString PackageHashError;
		double ReportPlayLength = 0.0;
		UAnimSequenceBase* Sequence =
			Cast<UAnimSequenceBase>(
				Template.AnimationAsset.LoadSynchronous()
			);
		if (
			!Sequence ||
			!Sequence->GetSkeleton() ||
			!Report->TryGetStringField(
				TEXT("source_asset_path"),
				ReportAssetPath
			) ||
			!Report->TryGetStringField(
				TEXT("source_asset_skeleton"),
				ReportSkeletonPath
			) ||
			!Report->TryGetNumberField(
				TEXT("source_asset_play_length_seconds"),
				ReportPlayLength
			) ||
			!Report->TryGetStringField(
				TEXT("source_asset_package_hash"),
				ReportPackageHash
			) ||
			!FLLMNPCAnimationTemplateDraftImporter::
				BuildAnimationAssetPackageHash(
					*Sequence,
					CurrentPackageHash,
					PackageHashError
				) ||
			ReportAssetPath !=
				Template.AnimationAsset.ToSoftObjectPath().ToString() ||
			ReportSkeletonPath !=
				Sequence->GetSkeleton()->GetPathName() ||
			ReportPackageHash != CurrentPackageHash ||
			!FMath::IsNearlyEqual(
				ReportPlayLength,
				static_cast<double>(Sequence->GetPlayLength()),
				1.e-6
			)
		)
		{
			OutError = TEXT("LLMNPC_AUTHORING_QUALITY_REPORT_STALE");
			return false;
		}
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

bool ULLMNPCTemplateAuthoringSubsystem::ValidateTemplateCatalogForPublish(
	const ULLMNPCMotionTemplate& Template,
	FString& OutError
)
{
	if (Template.Metadata.VisualDescription.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LLMNPC_AUTHORING_VISUAL_DESCRIPTION_REQUIRED");
		return false;
	}
	ULLMNPCActionVocabulary* Vocabulary = nullptr;
	const ULLMNPCPublicActionDefinition* Definition = nullptr;
	if (!LoadPublishedDefinitionForAction(
		Template.Metadata.PublicActionId,
		Vocabulary,
		Definition,
		OutError
	))
	{
		return false;
	}
	if (
		Template.Metadata.bRequiresTarget != Definition->bRequiresTarget ||
		!HaveSameNames(
			Template.Metadata.TargetCategoryTags,
			Definition->TargetCategoryTags
		)
	)
	{
		OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_TARGET_CONTRACT_MISMATCH");
		return false;
	}
	for (const FName Effect : Template.Metadata.SemanticEffectTags)
	{
		if (!Definition->SemanticEffectTags.Contains(Effect))
		{
			OutError = TEXT("LLMNPC_AUTHORING_PUBLIC_ACTION_EFFECT_MISMATCH");
			return false;
		}
	}
	if (!ValidateTemplateVocabularyTags(Template, *Vocabulary, OutError))
	{
		return false;
	}

	ULLMNPCMotionTemplate* PublishCandidate =
		DuplicateObject<ULLMNPCMotionTemplate>(&Template, GetTransientPackage());
	PublishCandidate->Metadata.ReviewState = ELLMNPCTemplateReviewState::Published;
	PublishCandidate->Metadata.CatalogContentHash.Reset();
	PublishCandidate->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(*PublishCandidate);
	return PublishCandidate->ValidateTemplate(OutError);
}
