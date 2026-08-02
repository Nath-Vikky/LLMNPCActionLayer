#include "Authoring/LLMNPCTemplateAuthoringSubsystem.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Authoring/LLMNPCAnimationTemplateDraftImporter.h"
#include "Authoring/LLMNPCMotionRecipeAuthoringPrompt.h"
#include "Authoring/LLMNPCTemplateDraftImporter.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "Dom/JsonObject.h"
#include "Engine/AssetManager.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/GameplayStatics.h"
#include "JsonObjectConverter.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCMotionValidator.h"
#include "LLMNPCSettings.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeCompiler.h"
#include "MotionRecipe/LLMNPCMotionRecipeParser.h"
#include "MotionRecipe/LLMNPCMotionRecipeValidator.h"
#include "ObjectTools.h"
#include "Quality/LLMNPCKinematicValidator.h"
#include "Sandbox/LLMNPCAuthoringSandbox.h"
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

TSharedRef<FJsonObject> BuildMotionRecipeAuthoringTrigger(
	const FLLMNPCMotionRecipeGenerationEvidence& Evidence
)
{
	TSharedRef<FJsonObject> Trigger = MakeShared<FJsonObject>();
	Trigger->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.motion_recipe_authoring_trigger.v1")
	);
	Trigger->SetStringField(
		TEXT("source"),
		Evidence.TriggerSource.ToString()
	);
	Trigger->SetStringField(
		TEXT("authoring_contract_id"),
		Evidence.AuthoringContractId.ToString()
	);
	if (
		Evidence.TriggerSource ==
			LLMNPCMotionRecipeAuthoring::
				RegenerationTriggerSource
	)
	{
		TSharedRef<FJsonObject> Parent = MakeShared<FJsonObject>();
		Parent->SetStringField(
			TEXT("template_id"),
			Evidence.SourceTemplateId.ToString()
		);
		Parent->SetStringField(
			TEXT("recipe_hash"),
			Evidence.SourceRecipeHash
		);
		Parent->SetStringField(
			TEXT("human_review_feedback"),
			Evidence.ReviewFeedback.TrimStartAndEnd()
		);
		Trigger->SetObjectField(TEXT("revision_parent"), Parent);
	}
	return Trigger;
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

ULLMNPCMotionTemplate* FindMotionTemplateById(FName TemplateId)
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")
		);
	FARFilter Filter;
	Filter.ClassPaths.Add(
		ULLMNPCMotionTemplate::StaticClass()->GetClassPathName()
	);
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	Assets.Sort(
		[](const FAssetData& A, const FAssetData& B)
		{
			return A.GetObjectPathString() <
				B.GetObjectPathString();
		}
	);
	for (const FAssetData& AssetData : Assets)
	{
		ULLMNPCMotionTemplate* Template =
			Cast<ULLMNPCMotionTemplate>(AssetData.GetAsset());
		if (
			Template &&
			Template->Metadata.TemplateId == TemplateId
		)
		{
			return Template;
		}
	}
	return nullptr;
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

ULLMNPCPublicActionDefinition* FindPublicActionDefinition(
	FName PublicActionId
)
{
	FAssetRegistryModule& AssetRegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
			TEXT("AssetRegistry")
		);
	FARFilter Filter;
	Filter.ClassPaths.Add(
		ULLMNPCPublicActionDefinition::StaticClass()->GetClassPathName()
	);
	Filter.bRecursiveClasses = true;
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssets(Filter, Assets);
	Assets.Sort(
		[](const FAssetData& A, const FAssetData& B)
		{
			return A.GetObjectPathString() < B.GetObjectPathString();
		}
	);
	for (const FAssetData& AssetData : Assets)
	{
		ULLMNPCPublicActionDefinition* Definition =
			Cast<ULLMNPCPublicActionDefinition>(AssetData.GetAsset());
		if (
			Definition &&
			Definition->PublicActionId == PublicActionId
		)
		{
			return Definition;
		}
	}
	return nullptr;
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

struct FLLMNPCRecipeQualityIdentity
{
	FString RecipeHash;
	FString CompiledRecipeHash;
	FString CapabilityHash;
	FString RegistryVersion;
	FString CompilerVersion;
};

bool IsMotionRecipeProvenance(
	const TSharedPtr<FJsonObject>& Provenance
)
{
	FString SourceType;
	return
		Provenance.IsValid() &&
		Provenance->TryGetStringField(TEXT("source_type"), SourceType) &&
		SourceType == TEXT("motion_recipe");
}

struct FLLMNPCMotionRecipeTriggerIdentity
{
	FString Source;
	FString ParentTemplateId;
	FString ParentRecipeHash;
	FString ReviewFeedback;
};

bool ReadMotionRecipeTriggerIdentity(
	const TSharedPtr<FJsonObject>& Trigger,
	FLLMNPCMotionRecipeTriggerIdentity& OutIdentity
)
{
	OutIdentity = FLLMNPCMotionRecipeTriggerIdentity();
	FString SchemaVersion;
	if (
		!Trigger.IsValid() ||
		!Trigger->TryGetStringField(
			TEXT("schema_version"),
			SchemaVersion
		) ||
		SchemaVersion !=
			TEXT("llmnpc.motion_recipe_authoring_trigger.v1") ||
		!Trigger->TryGetStringField(
			TEXT("source"),
			OutIdentity.Source
		)
	)
	{
		return false;
	}
	if (
		OutIdentity.Source ==
			LLMNPCMotionRecipeAuthoring::ManualTriggerSource
	)
	{
		return !Trigger->HasField(TEXT("revision_parent"));
	}
	if (
		OutIdentity.Source !=
			LLMNPCMotionRecipeAuthoring::
				RegenerationTriggerSource
	)
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Parent = nullptr;
	return
		Trigger->TryGetObjectField(
			TEXT("revision_parent"),
			Parent
		) &&
		Parent &&
		Parent->IsValid() &&
		(*Parent)->TryGetStringField(
			TEXT("template_id"),
			OutIdentity.ParentTemplateId
		) &&
		!OutIdentity.ParentTemplateId.IsEmpty() &&
		(*Parent)->TryGetStringField(
			TEXT("recipe_hash"),
			OutIdentity.ParentRecipeHash
		) &&
		!OutIdentity.ParentRecipeHash.IsEmpty() &&
		(*Parent)->TryGetStringField(
			TEXT("human_review_feedback"),
			OutIdentity.ReviewFeedback
		) &&
		!OutIdentity.ReviewFeedback.IsEmpty();
}

bool ValidateMotionRecipeEvidenceEnvelope(
	const TSharedPtr<FJsonObject>& Provenance,
	FString& OutError
)
{
	OutError.Reset();
	if (!IsMotionRecipeProvenance(Provenance))
	{
		OutError = TEXT("LLMNPC_RECIPE_PROVENANCE_SOURCE_TYPE_INVALID");
		return false;
	}
	const TSharedPtr<FJsonObject>* Recipe = nullptr;
	const TSharedPtr<FJsonObject>* License = nullptr;
	const TSharedPtr<FJsonObject>* Agent = nullptr;
	const TSharedPtr<FJsonObject>* ImportRecord = nullptr;
	const TSharedPtr<FJsonObject>* AuthoringTrigger = nullptr;
	FString RecipeHash;
	FString CompiledRecipeHash;
	FString CapabilityHash;
	FString RegistryVersion;
	FString CompilerVersion;
	FString PromptVersion;
	FString PromptHash;
	FString JobHash;
	FString JobPath;
	if (
		!Provenance->TryGetObjectField(
			TEXT("motion_recipe"),
			Recipe
		) ||
		!Recipe ||
		!Recipe->IsValid() ||
		!Provenance->TryGetObjectField(
			TEXT("source_license"),
			License
		) ||
		!License ||
		!License->IsValid() ||
		!Provenance->TryGetObjectField(
			TEXT("authoring_agent"),
			Agent
		) ||
		!Agent ||
		!Agent->IsValid() ||
		!Provenance->TryGetObjectField(
			TEXT("import_record"),
			ImportRecord
		) ||
		!ImportRecord ||
		!ImportRecord->IsValid() ||
		!Provenance->TryGetObjectField(
			TEXT("authoring_trigger"),
			AuthoringTrigger
		) ||
		!AuthoringTrigger ||
		!AuthoringTrigger->IsValid() ||
		!Provenance->TryGetStringField(
			TEXT("recipe_hash"),
			RecipeHash
		) ||
		RecipeHash.IsEmpty() ||
		!Provenance->TryGetStringField(
			TEXT("compiled_recipe_hash"),
			CompiledRecipeHash
		) ||
		CompiledRecipeHash.IsEmpty() ||
		!Provenance->TryGetStringField(
			TEXT("capability_hash"),
			CapabilityHash
		) ||
		CapabilityHash.IsEmpty() ||
		!Provenance->TryGetStringField(
			TEXT("primitive_registry_version"),
			RegistryVersion
		) ||
		RegistryVersion.IsEmpty() ||
		!Provenance->TryGetStringField(
			TEXT("compiler_version"),
			CompilerVersion
		) ||
		CompilerVersion.IsEmpty() ||
		!Provenance->TryGetStringField(
			TEXT("authoring_prompt_version"),
			PromptVersion
		) ||
		(
			PromptVersion !=
				LLMNPCMotionRecipeAuthoring::PromptVersion &&
			PromptVersion !=
				LLMNPCMotionRecipeAuthoring::
					LegacyPromptVersionV4 &&
			PromptVersion !=
				LLMNPCMotionRecipeAuthoring::
					LegacyPromptVersionV3
		) ||
		!Provenance->TryGetStringField(
			TEXT("authoring_prompt_hash"),
			PromptHash
		) ||
		PromptHash.IsEmpty() ||
		!Provenance->TryGetStringField(
			TEXT("authoring_job_hash"),
			JobHash
		) ||
		JobHash.IsEmpty() ||
		!(*ImportRecord)->TryGetStringField(
			TEXT("draft_source_copy_path"),
			JobPath
		) ||
		JobPath.IsEmpty()
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_PROVENANCE_FIELDS_MISSING");
		return false;
	}
	FString ModelId;
	FString ProviderId;
	FString ConfigHash;
	if (
		!(*Agent)->TryGetStringField(
			TEXT("provider_id"),
			ProviderId
		) ||
		ProviderId.IsEmpty() ||
		!(*Agent)->TryGetStringField(TEXT("model_id"), ModelId) ||
		ModelId.IsEmpty() ||
		!(*Agent)->TryGetStringField(
			TEXT("non_secret_config_hash"),
			ConfigHash
		) ||
		ConfigHash.IsEmpty()
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_PROVENANCE_AGENT_INVALID");
		return false;
	}
	FString JobJson;
	if (
		!FFileHelper::LoadFileToString(JobJson, *JobPath) ||
		FLLMNPCUEPIArtifactAdapter::HashJson(JobJson) != JobHash
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_PROVENANCE_JOB_MISSING_OR_CHANGED");
		return false;
	}
	const FString JobLower = JobJson.ToLower();
	if (
		JobLower.Contains(TEXT("openai_api_key")) ||
		JobLower.Contains(TEXT("authorization")) ||
		JobLower.Contains(TEXT("bearer "))
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_PROVENANCE_JOB_SECRET_MARKER");
		return false;
	}
	TSharedPtr<FJsonObject> Job;
	const TSharedPtr<FJsonObject>* JobTrigger = nullptr;
	FString JobSchemaVersion;
	FLLMNPCMotionRecipeTriggerIdentity ProvenanceTriggerIdentity;
	FLLMNPCMotionRecipeTriggerIdentity JobTriggerIdentity;
	if (
		!ParseJsonObject(JobJson, Job) ||
		!Job->TryGetStringField(
			TEXT("schema_version"),
			JobSchemaVersion
		) ||
		JobSchemaVersion !=
			LLMNPCMotionRecipeAuthoring::JobSchemaVersion ||
		!Job->TryGetObjectField(
			TEXT("authoring_trigger"),
			JobTrigger
		) ||
		!JobTrigger ||
		!JobTrigger->IsValid() ||
		!ReadMotionRecipeTriggerIdentity(
			*AuthoringTrigger,
			ProvenanceTriggerIdentity
		) ||
		!ReadMotionRecipeTriggerIdentity(
			*JobTrigger,
			JobTriggerIdentity
		) ||
		ProvenanceTriggerIdentity.Source !=
			JobTriggerIdentity.Source ||
		ProvenanceTriggerIdentity.ParentTemplateId !=
			JobTriggerIdentity.ParentTemplateId ||
		ProvenanceTriggerIdentity.ParentRecipeHash !=
			JobTriggerIdentity.ParentRecipeHash ||
		ProvenanceTriggerIdentity.ReviewFeedback !=
			JobTriggerIdentity.ReviewFeedback
	)
	{
		OutError =
			TEXT("LLMNPC_RECIPE_PROVENANCE_TRIGGER_INVALID");
		return false;
	}
	return true;
}

bool RecompileMotionRecipeTemplate(
	const ULLMNPCMotionTemplate& Template,
	const TSharedPtr<FJsonObject>& Provenance,
	FLLMNPCRecipeQualityIdentity& OutIdentity,
	FString& OutError
)
{
	OutIdentity = FLLMNPCRecipeQualityIdentity();
	OutError.Reset();
	const TSharedPtr<FJsonObject>* RecipeObject = nullptr;
	if (
		!IsMotionRecipeProvenance(Provenance) ||
		!Provenance->TryGetObjectField(
			TEXT("motion_recipe"),
			RecipeObject
		) ||
		!RecipeObject ||
		!RecipeObject->IsValid()
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_PROVENANCE_RECIPE_MISSING");
		return false;
	}
	FString RecipeJson;
	if (!SerializeJsonObject((*RecipeObject).ToSharedRef(), RecipeJson))
	{
		OutError = TEXT("LLMNPC_RECIPE_PROVENANCE_RECIPE_SERIALIZE_FAILED");
		return false;
	}

	ULLMNPCSkeletonProfile* Profile =
		FindSkeletonProfile(Template.Metadata.SkeletonProfileId);
	FString ProfileError;
	if (!Profile || !Profile->ValidateProfile(ProfileError))
	{
		OutError = ProfileError.IsEmpty()
			? TEXT("LLMNPC_RECIPE_QUALITY_PROFILE_MISSING")
			: ProfileError;
		return false;
	}
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	const FLLMNPCSkeletonCapabilityBuildResult CapabilityResult =
		FLLMNPCSkeletonCapabilityBuilder::Build(
			*Profile,
			nullptr,
			Capability
		);
	if (!CapabilityResult.bSucceeded)
	{
		OutError = CapabilityResult.Errors.IsEmpty()
			? TEXT("LLMNPC_RECIPE_QUALITY_CAPABILITY_BUILD_FAILED")
			: CapabilityResult.Errors[0];
		return false;
	}

	FLLMNPCMotionRecipe Recipe;
	if (!FLLMNPCMotionRecipeParser::Parse(
		RecipeJson,
		Recipe,
		OutError
	))
	{
		return false;
	}
	FLLMNPCMotionRecipeCompileContext CompileContext;
	CompileContext.ValidationContext.Mode =
		ELLMNPCMotionRecipeMode::AuthoringSandbox;
	const TSharedPtr<FJsonObject>* AuthoringTrigger = nullptr;
	FString AuthoringContractId;
	if (
		!Provenance->TryGetObjectField(
			TEXT("authoring_trigger"),
			AuthoringTrigger
		) ||
		!AuthoringTrigger ||
		!AuthoringTrigger->IsValid() ||
		!(*AuthoringTrigger)->TryGetStringField(
			TEXT("authoring_contract_id"),
			AuthoringContractId
		)
	)
	{
		OutError =
			TEXT("LLMNPC_RECIPE_QUALITY_AUTHORING_CONTRACT_MISSING");
		return false;
	}
	const FLLMNPCMotionRecipeAuthoringContract* AuthoringContract =
		FLLMNPCMotionRecipeAuthoringPrompt::FindContract(
			FName(*AuthoringContractId)
		);
	if (!AuthoringContract)
	{
		OutError =
			TEXT("LLMNPC_RECIPE_QUALITY_AUTHORING_CONTRACT_INVALID");
		return false;
	}
	CompileContext.ValidationContext.AllowedTargetSlots =
		AuthoringContract->AllowedTargetSlots;
	for (const FName TargetSlot :
		AuthoringContract->AllowedTargetSlots)
	{
		CompileContext.TargetBindings.Add(
			TargetSlot,
			FLLMNPCAuthoringSandbox::
				BuildCanonicalTargetRef(TargetSlot)
		);
	}
	FLLMMotionPlan Plan;
	FLLMNPCCompiledRecipeMetadata Compiled;
	if (!FLLMNPCMotionRecipeCompiler::Compile(
		Recipe,
		Capability,
		FLLMNPCMotionPrimitiveRegistry::Get(),
		CompileContext,
		Plan,
		Compiled,
		OutError
	))
	{
		return false;
	}

	FString ExpectedRecipeHash;
	FString ExpectedCompiledHash;
	FString ExpectedCapabilityHash;
	FString ExpectedRegistryVersion;
	FString ExpectedCompilerVersion;
	if (
		!Provenance->TryGetStringField(
			TEXT("recipe_hash"),
			ExpectedRecipeHash
		) ||
		!Provenance->TryGetStringField(
			TEXT("compiled_recipe_hash"),
			ExpectedCompiledHash
		) ||
		!Provenance->TryGetStringField(
			TEXT("capability_hash"),
			ExpectedCapabilityHash
		) ||
		!Provenance->TryGetStringField(
			TEXT("primitive_registry_version"),
			ExpectedRegistryVersion
		) ||
		!Provenance->TryGetStringField(
			TEXT("compiler_version"),
			ExpectedCompilerVersion
		) ||
		Compiled.RecipeHash != ExpectedRecipeHash ||
		Compiled.CompiledRecipeHash != ExpectedCompiledHash ||
		Compiled.CapabilityHash != ExpectedCapabilityHash ||
		Compiled.PrimitiveRegistryVersion != ExpectedRegistryVersion ||
		Compiled.CompilerVersion != ExpectedCompilerVersion ||
		Template.Metadata.SourceRecipeHash != Compiled.RecipeHash
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_QUALITY_IDENTITY_MISMATCH");
		return false;
	}

	FString StoredClipJson;
	FString RecompiledClipJson;
	if (
		!FJsonObjectConverter::UStructToJsonObjectString(
			Template.ProceduralClip,
			StoredClipJson
		) ||
		!FJsonObjectConverter::UStructToJsonObjectString(
			Plan.Clip,
			RecompiledClipJson
		) ||
		FLLMNPCUEPIArtifactAdapter::HashJson(StoredClipJson) !=
			FLLMNPCUEPIArtifactAdapter::HashJson(RecompiledClipJson)
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_QUALITY_COMPILED_CLIP_MISMATCH");
		return false;
	}

	OutIdentity.RecipeHash = Compiled.RecipeHash;
	OutIdentity.CompiledRecipeHash = Compiled.CompiledRecipeHash;
	OutIdentity.CapabilityHash = Compiled.CapabilityHash;
	OutIdentity.RegistryVersion = Compiled.PrimitiveRegistryVersion;
	OutIdentity.CompilerVersion = Compiled.CompilerVersion;
	return true;
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

FLLMNPCAuthoringOperationResult
ULLMNPCTemplateAuthoringSubsystem::CreateMotionRecipeDraft(
	const FString& RecipeJson,
	FName SkeletonProfileId,
	const FLLMNPCMotionRecipeDraftCatalogSpec& CatalogSpec,
	const FLLMNPCMotionRecipeGenerationEvidence& Evidence,
	const FString& DestinationPackagePath,
	const FString& PublicActionDraftDestinationPath
)
{
	FString DirectoryError;
	if (!EnsureAuthoringDirectories(DirectoryError))
	{
		return ErrorResult(
			TEXT("LLMNPC_AUTHORING_DIRECTORY_FAILED"),
			DirectoryError
		);
	}
	if (
		SkeletonProfileId.IsNone() ||
		CatalogSpec.AssetName.TrimStartAndEnd().IsEmpty() ||
		CatalogSpec.TemplateId.IsNone() ||
		CatalogSpec.PublicActionId.IsNone() ||
		CatalogSpec.PublicActionAssetName.TrimStartAndEnd().IsEmpty() ||
		CatalogSpec.DisplayName.TrimStartAndEnd().IsEmpty() ||
		CatalogSpec.SelectionSummary.TrimStartAndEnd().IsEmpty() ||
		CatalogSpec.SelectionSummary.Len() > 240 ||
		CatalogSpec.VisualDescription.TrimStartAndEnd().IsEmpty() ||
		CatalogSpec.VisualDescription.Len() > 600 ||
		CatalogSpec.SuitableWhen.IsEmpty() ||
		CatalogSpec.AvoidWhen.IsEmpty() ||
		CatalogSpec.GestureFamily.IsNone() ||
		CatalogSpec.SemanticEffectTags.IsEmpty()
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_CATALOG_SPEC_INVALID"),
			TEXT("Motion Recipe catalog metadata is incomplete or exceeds its bounded text limits.")
		);
	}
	if (
		!Evidence.RequestId.IsValid() ||
		Evidence.ProviderId.IsNone() ||
		Evidence.ProviderModelId.TrimStartAndEnd().IsEmpty() ||
		Evidence.NonSecretConfigHash.TrimStartAndEnd().IsEmpty() ||
		Evidence.AuthoringContractId.IsNone() ||
		Evidence.PromptVersion !=
			LLMNPCMotionRecipeAuthoring::PromptVersion ||
		Evidence.PromptHash.TrimStartAndEnd().IsEmpty() ||
		Evidence.SystemPrompt.TrimStartAndEnd().IsEmpty() ||
		Evidence.UserJson.TrimStartAndEnd().IsEmpty() ||
		Evidence.RawResponseJson.TrimStartAndEnd().IsEmpty() ||
		Evidence.GeneratedAtUtc.GetTicks() <= 0
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_ONLINE_EVIDENCE_INVALID"),
			TEXT("A complete online Authoring Model evidence record is required.")
		);
	}
	const FString StablePrompt = FString::Printf(
		TEXT("%s\n%s\n%s"),
		*Evidence.PromptVersion,
		*Evidence.SystemPrompt,
		*Evidence.UserJson
	);
	const FString ExpectedPromptHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*StablePrompt)
	);
	if (ExpectedPromptHash != Evidence.PromptHash)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_PROMPT_HASH_MISMATCH"),
			TEXT("The Authoring Prompt no longer matches its evidence hash.")
		);
	}
	const FString EvidenceLower = FString::Printf(
		TEXT("%s\n%s\n%s\n%s"),
		*Evidence.SystemPrompt,
		*Evidence.UserJson,
		*Evidence.RecipeSchemaJson,
		*Evidence.CapabilityModelViewJson
	).ToLower();
	if (
		EvidenceLower.Contains(TEXT("openai_api_key")) ||
		EvidenceLower.Contains(TEXT("authorization")) ||
		EvidenceLower.Contains(TEXT("bearer "))
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_EVIDENCE_SECRET_MARKER"),
			TEXT("Authoring evidence contains a forbidden credential marker.")
		);
	}

	TSharedPtr<FJsonObject> UserRequestObject;
	FString RequestSchemaVersion;
	FString RequestTriggerSource;
	const TSharedPtr<FJsonObject>* AuthoringConstraints = nullptr;
	FString RequestContractId;
	if (
		!ParseJsonObject(Evidence.UserJson, UserRequestObject) ||
		!UserRequestObject->TryGetStringField(
			TEXT("schema_version"),
			RequestSchemaVersion
		) ||
		RequestSchemaVersion !=
			TEXT("llmnpc.motion_recipe_authoring_request.v2") ||
		!UserRequestObject->TryGetStringField(
			TEXT("trigger_source"),
			RequestTriggerSource
		) ||
		RequestTriggerSource != Evidence.TriggerSource.ToString()
		||
		!UserRequestObject->TryGetObjectField(
			TEXT("authoring_constraints"),
			AuthoringConstraints
		) ||
		!AuthoringConstraints ||
		!AuthoringConstraints->IsValid() ||
		!(*AuthoringConstraints)->TryGetStringField(
			TEXT("contract_id"),
			RequestContractId
		) ||
		RequestContractId != Evidence.AuthoringContractId.ToString()
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_REQUEST_EVIDENCE_INVALID"),
			TEXT("The Authoring trigger or contract is missing or does not match its request evidence.")
		);
	}
	const FLLMNPCMotionRecipeAuthoringContract* AuthoringContract =
		FLLMNPCMotionRecipeAuthoringPrompt::FindContract(
			Evidence.AuthoringContractId
		);
	if (!AuthoringContract)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_CONTRACT_UNKNOWN"),
			TEXT("The Authoring contract is not registered.")
		);
	}

	const bool bManualTrigger =
		Evidence.TriggerSource ==
			LLMNPCMotionRecipeAuthoring::ManualTriggerSource;
	const bool bRegenerationTrigger =
		Evidence.TriggerSource ==
			LLMNPCMotionRecipeAuthoring::
				RegenerationTriggerSource;
	ULLMNPCMotionTemplate* RegenerationSource = nullptr;
	if (!bManualTrigger && !bRegenerationTrigger)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_TRIGGER_SOURCE_UNKNOWN"),
			TEXT("The Authoring trigger source is not allowed.")
		);
	}
	if (bManualTrigger)
	{
		if (
			UserRequestObject->HasField(TEXT("revision_context")) ||
			!Evidence.SourceTemplateId.IsNone() ||
			!Evidence.SourceRecipeHash.IsEmpty() ||
			!Evidence.ReviewFeedback.IsEmpty()
		)
		{
			return ErrorResult(
				TEXT("LLMNPC_RECIPE_DRAFT_MANUAL_LINEAGE_INVALID"),
				TEXT("A ManualWorkbench request cannot inherit rejected Draft lineage.")
			);
		}
	}
	else
	{
		const TSharedPtr<FJsonObject>* RevisionContext = nullptr;
		FString RequestSourceTemplateId;
		FString RequestSourceRecipeHash;
		FString RequestReviewFeedback;
		if (
			!UserRequestObject->TryGetObjectField(
				TEXT("revision_context"),
				RevisionContext
			) ||
			!RevisionContext ||
			!RevisionContext->IsValid() ||
			!(*RevisionContext)->TryGetStringField(
				TEXT("source_template_id"),
				RequestSourceTemplateId
			) ||
			!(*RevisionContext)->TryGetStringField(
				TEXT("source_recipe_hash"),
				RequestSourceRecipeHash
			) ||
			!(*RevisionContext)->TryGetStringField(
				TEXT("human_review_feedback"),
				RequestReviewFeedback
			) ||
			RequestSourceTemplateId !=
				Evidence.SourceTemplateId.ToString() ||
			RequestSourceRecipeHash !=
				Evidence.SourceRecipeHash ||
			RequestReviewFeedback !=
				Evidence.ReviewFeedback.TrimStartAndEnd() ||
			Evidence.SourceTemplateId.IsNone() ||
			Evidence.SourceRecipeHash.IsEmpty() ||
			Evidence.ReviewFeedback.TrimStartAndEnd().IsEmpty() ||
			Evidence.ReviewFeedback.Len() > 600
		)
		{
			return ErrorResult(
				TEXT("LLMNPC_RECIPE_DRAFT_REVISION_EVIDENCE_INVALID"),
				TEXT("Rejected Draft lineage is incomplete or does not match the online request.")
			);
		}

		RegenerationSource =
			FindMotionTemplateById(Evidence.SourceTemplateId);
		if (
			!RegenerationSource ||
			RegenerationSource->Metadata.ReviewState !=
				ELLMNPCTemplateReviewState::Rejected ||
			RegenerationSource->Metadata.SourceRecipeHash !=
				Evidence.SourceRecipeHash ||
			RegenerationSource->Metadata.SkeletonProfileId !=
				SkeletonProfileId ||
			RegenerationSource->Metadata.PublicActionId !=
				CatalogSpec.PublicActionId
		)
		{
			return ErrorResult(
				TEXT("LLMNPC_RECIPE_DRAFT_REVISION_PARENT_INVALID"),
				TEXT("The regeneration parent must remain a matching Rejected Motion Recipe Draft.")
			);
		}
	}

	ULLMNPCSkeletonProfile* SkeletonProfile =
		FindSkeletonProfile(SkeletonProfileId);
	FString ProfileError;
	if (
		!SkeletonProfile ||
		!SkeletonProfile->ValidateProfile(ProfileError)
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_SKELETON_PROFILE_INVALID"),
			ProfileError.IsEmpty()
				? TEXT("The selected Skeleton Profile was not found.")
				: ProfileError
		);
	}
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	const FLLMNPCSkeletonCapabilityBuildResult CapabilityResult =
		FLLMNPCSkeletonCapabilityBuilder::Build(
			*SkeletonProfile,
			nullptr,
			Capability
		);
	if (!CapabilityResult.bSucceeded)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_CAPABILITY_BUILD_FAILED"),
			CapabilityResult.Errors.IsEmpty()
				? TEXT("The Skeleton Capability could not be built.")
				: CapabilityResult.Errors[0]
		);
	}
	if (
		Evidence.CapabilityHash != Capability.CapabilityHash ||
		Evidence.RegistryVersion !=
			FLLMNPCMotionPrimitiveRegistry::Get().GetRegistryVersion()
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_CONTEXT_HASH_MISMATCH"),
			TEXT("Capability or Primitive Registry changed after online generation.")
		);
	}
	FString RestrictedField;
	if (
		FLLMNPCSkeletonCapabilityBuilder::ModelViewContainsRestrictedFields(
			Evidence.CapabilityModelViewJson,
			RestrictedField
		)
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_CAPABILITY_EVIDENCE_RESTRICTED"),
			FString::Printf(
				TEXT("Capability evidence exposes a restricted field: %s"),
				*RestrictedField
			)
		);
	}
	FString CurrentRecipeSchema;
	FString ContextError;
	if (
		!FLLMNPCMotionPrimitiveRegistry::Get().BuildModelSchemaJson(
			&Capability,
			CurrentRecipeSchema,
			ContextError
		) ||
		FLLMNPCUEPIArtifactAdapter::HashJson(CurrentRecipeSchema) !=
			FLLMNPCUEPIArtifactAdapter::HashJson(
				Evidence.RecipeSchemaJson
			)
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_SCHEMA_EVIDENCE_STALE"),
			ContextError.IsEmpty()
				? TEXT("The generated Recipe Schema evidence is stale.")
				: ContextError
		);
	}

	FLLMNPCMotionRecipeAuthoringResponse OnlineResponse;
	FString Error;
	if (
		!FLLMNPCMotionRecipeAuthoringPrompt::ParseResponse(
			Evidence.RawResponseJson,
			OnlineResponse,
			Error
		) ||
		OnlineResponse.bUnsupported
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_ONLINE_RESPONSE_INVALID"),
			Error.IsEmpty()
				? TEXT("The Authoring Model did not return a Recipe.")
				: Error
		);
	}
	if (
		!FLLMNPCMotionRecipeAuthoringPrompt::
			ValidateRecipeForCapability(
				OnlineResponse,
				Capability,
				*AuthoringContract,
				Error
			)
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_CONTRACT_VALIDATION_FAILED"),
			Error
		);
	}

	const FLLMNPCMotionPrimitiveRegistry& Registry =
		FLLMNPCMotionPrimitiveRegistry::Get();
	FLLMNPCMotionRecipeValidationContext ValidationContext;
	ValidationContext.Mode = ELLMNPCMotionRecipeMode::AuthoringSandbox;
	ValidationContext.AllowedTargetSlots =
		AuthoringContract->AllowedTargetSlots;

	auto NormalizeRecipe = [&](
		const FString& SourceJson,
		FLLMNPCMotionRecipe& OutRecipe,
		FLLMNPCMotionRecipeValidationResult& OutValidation,
		FString& OutCanonical,
		FString& OutNormalizeError
	)
	{
		if (!FLLMNPCMotionRecipeParser::Parse(
			SourceJson,
			OutRecipe,
			OutNormalizeError
		))
		{
			return false;
		}
		if (!FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			OutRecipe,
			Capability,
			Registry,
			ValidationContext,
			OutValidation
		))
		{
			OutNormalizeError = OutValidation.ErrorCode;
			return false;
		}
		return FLLMNPCMotionRecipeCanonicalizer::BuildCanonicalJson(
			OutRecipe,
			OutCanonical,
			OutNormalizeError
		);
	};

	FLLMNPCMotionRecipe Recipe;
	FLLMNPCMotionRecipeValidationResult Validation;
	FString CanonicalRecipe;
	if (!NormalizeRecipe(
		RecipeJson,
		Recipe,
		Validation,
		CanonicalRecipe,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}
	FLLMNPCMotionRecipe ResponseRecipe;
	FLLMNPCMotionRecipeValidationResult ResponseValidation;
	FString CanonicalResponseRecipe;
	if (!NormalizeRecipe(
		OnlineResponse.RecipeJson,
		ResponseRecipe,
		ResponseValidation,
		CanonicalResponseRecipe,
		Error
	))
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_RESPONSE_RECIPE_MISMATCH"),
			Error
		);
	}
	const bool bHumanParameterEdit =
		CanonicalResponseRecipe != CanonicalRecipe;
	if (bHumanParameterEdit)
	{
		bool bSameStructure =
			ResponseRecipe.RecipeId == Recipe.RecipeId &&
			ResponseRecipe.Intent == Recipe.Intent &&
			ResponseRecipe.Primitives.Num() ==
				Recipe.Primitives.Num();
		for (
			int32 PrimitiveIndex = 0;
			bSameStructure &&
			PrimitiveIndex < Recipe.Primitives.Num();
			++PrimitiveIndex
		)
		{
			const FLLMNPCMotionRecipePrimitive& Original =
				ResponseRecipe.Primitives[PrimitiveIndex];
			const FLLMNPCMotionRecipePrimitive& Edited =
				Recipe.Primitives[PrimitiveIndex];
			bSameStructure =
				Original.PrimitiveId == Edited.PrimitiveId &&
				Original.Side == Edited.Side &&
				Original.TargetSlot == Edited.TargetSlot;
		}
		if (!bSameStructure)
		{
			return ErrorResult(
				TEXT("LLMNPC_RECIPE_DRAFT_STRUCTURE_EDIT_FORBIDDEN"),
				TEXT("Workbench correction may tune validated timing and parameters, but changing the online Recipe structure requires a new generation request.")
			);
		}
	}
	const FString OnlineRecipeHash =
		FLLMNPCMotionRecipeCanonicalizer::BuildRecipeHash(
			CanonicalResponseRecipe
		);

	FLLMNPCMotionRecipeCompileContext CompileContext;
	CompileContext.ValidationContext = ValidationContext;
	for (const FName TargetSlot :
		AuthoringContract->AllowedTargetSlots)
	{
		CompileContext.TargetBindings.Add(
			TargetSlot,
			FLLMNPCAuthoringSandbox::
				BuildCanonicalTargetRef(TargetSlot)
		);
	}
	FLLMMotionPlan Plan;
	FLLMNPCCompiledRecipeMetadata CompiledMetadata;
	if (!FLLMNPCMotionRecipeCompiler::Compile(
		Recipe,
		Capability,
		Registry,
		CompileContext,
		Plan,
		CompiledMetadata,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}
	if (CompiledMetadata.RecipeHash.IsEmpty())
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_COMPILED_HASH_MISSING"),
			TEXT("The Recipe Compiler did not produce stable identity metadata.")
		);
	}
	if (
		!Evidence.CompiledRecipeHash.IsEmpty() &&
		Evidence.CompiledRecipeHash !=
			CompiledMetadata.CompiledRecipeHash
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_COMPILED_HASH_MISMATCH"),
			TEXT("The Recipe no longer compiles to the Sandbox-approved Motion Plan.")
		);
	}
	const FLLMNPCKinematicQualityReport KinematicReport =
		FLLMNPCKinematicValidator::ValidatePlan(
			Plan,
			*SkeletonProfile,
			nullptr,
			Capability.CapabilityHash
		);
	if (
		!KinematicReport.bPassed ||
		KinematicReport.ReportHash.IsEmpty()
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_KINEMATIC_PREFLIGHT_FAILED"),
			TEXT("The compiled Recipe did not pass deterministic kinematic validation.")
		);
	}
	if (
		!Evidence.KinematicReportHash.IsEmpty() &&
		Evidence.KinematicReportHash !=
			KinematicReport.ReportHash
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_KINEMATIC_HASH_MISMATCH"),
			TEXT("The Recipe kinematic report no longer matches the Sandbox-approved report.")
		);
	}

	TSharedPtr<FJsonObject> CanonicalRecipeObject;
	TSharedPtr<FJsonObject> RawResponseObject;
	if (
		!ParseJsonObject(CanonicalRecipe, CanonicalRecipeObject) ||
		!ParseJsonObject(Evidence.RawResponseJson, RawResponseObject)
	)
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_EVIDENCE_JSON_INVALID"),
			TEXT("Canonical Recipe or online evidence JSON could not be parsed.")
		);
	}

	TSharedRef<FJsonObject> Job = MakeShared<FJsonObject>();
	Job->SetStringField(
		TEXT("schema_version"),
		LLMNPCMotionRecipeAuthoring::JobSchemaVersion
	);
	Job->SetStringField(
		TEXT("request_id"),
		Evidence.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)
	);
	Job->SetStringField(
		TEXT("generated_at_utc"),
		Evidence.GeneratedAtUtc.ToIso8601()
	);
	Job->SetStringField(
		TEXT("provider_id"),
		Evidence.ProviderId.ToString()
	);
	Job->SetStringField(
		TEXT("provider_model_id"),
		Evidence.ProviderModelId
	);
	Job->SetStringField(
		TEXT("endpoint_origin"),
		Evidence.EndpointOrigin
	);
	Job->SetStringField(
		TEXT("non_secret_config_hash"),
		Evidence.NonSecretConfigHash
	);
	Job->SetStringField(
		TEXT("prompt_version"),
		Evidence.PromptVersion
	);
	Job->SetStringField(
		TEXT("authoring_contract_id"),
		Evidence.AuthoringContractId.ToString()
	);
	Job->SetStringField(TEXT("prompt_hash"), Evidence.PromptHash);
	Job->SetStringField(
		TEXT("capability_hash"),
		CompiledMetadata.CapabilityHash
	);
	Job->SetStringField(
		TEXT("primitive_registry_version"),
		CompiledMetadata.PrimitiveRegistryVersion
	);
	Job->SetStringField(
		TEXT("compiler_version"),
		CompiledMetadata.CompilerVersion
	);
	Job->SetStringField(
		TEXT("recipe_hash"),
		CompiledMetadata.RecipeHash
	);
	Job->SetStringField(
		TEXT("online_recipe_hash"),
		OnlineRecipeHash
	);
	Job->SetBoolField(
		TEXT("human_parameter_edit"),
		bHumanParameterEdit
	);
	Job->SetStringField(
		TEXT("compiled_recipe_hash"),
		CompiledMetadata.CompiledRecipeHash
	);
	Job->SetStringField(
		TEXT("kinematic_report_hash"),
		KinematicReport.ReportHash
	);
	Job->SetStringField(
		TEXT("kinematic_baseline_hash"),
		SkeletonProfile->UpperBodyConstraints.ValidationBaselineHash
	);
	Job->SetStringField(
		TEXT("system_prompt"),
		Evidence.SystemPrompt
	);
	Job->SetObjectField(
		TEXT("authoring_trigger"),
		BuildMotionRecipeAuthoringTrigger(Evidence)
	);
	Job->SetObjectField(
		TEXT("authoring_request"),
		UserRequestObject.ToSharedRef()
	);
	Job->SetObjectField(
		TEXT("provider_response"),
		RawResponseObject.ToSharedRef()
	);
	Job->SetObjectField(
		TEXT("normalized_recipe"),
		CanonicalRecipeObject.ToSharedRef()
	);
	Job->SetNumberField(TEXT("http_status"), Evidence.HttpStatus);
	Job->SetNumberField(TEXT("attempt_count"), Evidence.AttemptCount);
	Job->SetNumberField(
		TEXT("latency_seconds"),
		Evidence.TotalLatencySeconds
	);
	Job->SetNumberField(TEXT("prompt_tokens"), Evidence.PromptTokens);
	Job->SetNumberField(
		TEXT("completion_tokens"),
		Evidence.CompletionTokens
	);
	Job->SetNumberField(TEXT("total_tokens"), Evidence.TotalTokens);

	FString JobJson;
	if (!SerializeJsonObject(Job, JobJson))
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_JOB_SERIALIZE_FAILED"),
			TEXT("The sanitized online Authoring Job could not be serialized.")
		);
	}
	const FString JobPayloadHash =
		FLLMNPCUEPIArtifactAdapter::HashJson(JobJson);
	FString SafeRequestId =
		Evidence.RequestId.ToString(EGuidFormats::Digits).Left(12);
	const FString JobPath = FPaths::Combine(
		GetDraftDirectory(),
		FString::Printf(
			TEXT("%s_online_%s.json"),
			*CatalogSpec.AssetName,
			*SafeRequestId
		)
	);
	if (!FFileHelper::SaveStringToFile(
		JobJson,
		*JobPath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_JOB_WRITE_FAILED"),
			TEXT("The sanitized online Authoring Job could not be preserved.")
		);
	}

	TSharedRef<FJsonObject> Provenance = MakeShared<FJsonObject>();
	Provenance->SetStringField(TEXT("source_type"), TEXT("motion_recipe"));
	Provenance->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.motion_recipe_provenance.v1")
	);
	Provenance->SetObjectField(
		TEXT("motion_recipe"),
		CanonicalRecipeObject.ToSharedRef()
	);
	Provenance->SetStringField(
		TEXT("recipe_hash"),
		CompiledMetadata.RecipeHash
	);
	Provenance->SetStringField(
		TEXT("online_recipe_hash"),
		OnlineRecipeHash
	);
	Provenance->SetBoolField(
		TEXT("human_parameter_edit"),
		bHumanParameterEdit
	);
	Provenance->SetStringField(
		TEXT("compiled_recipe_hash"),
		CompiledMetadata.CompiledRecipeHash
	);
	Provenance->SetStringField(
		TEXT("kinematic_report_hash"),
		KinematicReport.ReportHash
	);
	Provenance->SetStringField(
		TEXT("kinematic_baseline_hash"),
		SkeletonProfile->UpperBodyConstraints.ValidationBaselineHash
	);
	Provenance->SetStringField(
		TEXT("capability_hash"),
		CompiledMetadata.CapabilityHash
	);
	Provenance->SetStringField(
		TEXT("primitive_registry_version"),
		CompiledMetadata.PrimitiveRegistryVersion
	);
	Provenance->SetStringField(
		TEXT("compiler_version"),
		CompiledMetadata.CompilerVersion
	);
	Provenance->SetStringField(
		TEXT("authoring_prompt_version"),
		Evidence.PromptVersion
	);
	Provenance->SetStringField(
		TEXT("authoring_prompt_hash"),
		Evidence.PromptHash
	);
	Provenance->SetStringField(
		TEXT("authoring_job_hash"),
		JobPayloadHash
	);
	Provenance->SetObjectField(
		TEXT("authoring_trigger"),
		BuildMotionRecipeAuthoringTrigger(Evidence)
	);
	TSharedRef<FJsonObject> License = MakeShared<FJsonObject>();
	License->SetStringField(
		TEXT("identifier"),
		TEXT("project_owned_generated_motion_recipe")
	);
	License->SetStringField(TEXT("holder"), TEXT("project_owner"));
	License->SetBoolField(TEXT("redistribution_allowed"), true);
	Provenance->SetObjectField(TEXT("source_license"), License);
	TSharedRef<FJsonObject> Agent = MakeShared<FJsonObject>();
	Agent->SetStringField(
		TEXT("provider_id"),
		Evidence.ProviderId.ToString()
	);
	Agent->SetStringField(
		TEXT("model_id"),
		Evidence.ProviderModelId
	);
	Agent->SetStringField(
		TEXT("non_secret_config_hash"),
		Evidence.NonSecretConfigHash
	);
	Agent->SetStringField(
		TEXT("generated_at_utc"),
		Evidence.GeneratedAtUtc.ToIso8601()
	);
	Provenance->SetObjectField(TEXT("authoring_agent"), Agent);
	TSharedRef<FJsonObject> ImportRecord = MakeShared<FJsonObject>();
	ImportRecord->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.motion_recipe_draft_import.v1")
	);
	ImportRecord->SetStringField(
		TEXT("draft_content_hash"),
		JobPayloadHash
	);
	ImportRecord->SetStringField(
		TEXT("draft_source_copy_path"),
		FPaths::ConvertRelativePathToFull(JobPath)
	);
	ImportRecord->SetStringField(
		TEXT("imported_at_utc"),
		FDateTime::UtcNow().ToIso8601()
	);
	Provenance->SetObjectField(TEXT("import_record"), ImportRecord);

	ULLMNPCMotionTemplate* TemplateSeed =
		NewObject<ULLMNPCMotionTemplate>(GetTransientPackage());
	TemplateSeed->Kind = ELLMNPCTemplateKind::ProceduralMotion;
	TemplateSeed->Metadata.TemplateId = CatalogSpec.TemplateId;
	TemplateSeed->Metadata.PublicActionId = CatalogSpec.PublicActionId;
	TemplateSeed->Metadata.SemanticVersion = CatalogSpec.SemanticVersion;
	TemplateSeed->Metadata.CatalogSchemaVersion =
		LLMNPCCatalog::SchemaVersion;
	TemplateSeed->Metadata.CatalogRevision = 1;
	TemplateSeed->Metadata.VariantId = CatalogSpec.VariantId;
	TemplateSeed->Metadata.VariantWeight = 1.0f;
	TemplateSeed->Metadata.VariantStyleTags =
		CatalogSpec.VariantStyleTags;
	TemplateSeed->Metadata.DisplayName =
		FText::FromString(CatalogSpec.DisplayName);
	TemplateSeed->Metadata.Description =
		FText::FromString(CatalogSpec.SelectionSummary);
	TemplateSeed->Metadata.VisualDescription =
		CatalogSpec.VisualDescription;
	TemplateSeed->Metadata.IntentTags = CatalogSpec.IntentTags;
	TemplateSeed->Metadata.EmotionTags = CatalogSpec.EmotionTags;
	TemplateSeed->Metadata.BodyRegionTags =
		CatalogSpec.BodyRegionTags;
	TemplateSeed->Metadata.SpatialRequirementTags =
		CatalogSpec.SpatialRequirementTags;
	TemplateSeed->Metadata.SemanticEffectTags =
		CatalogSpec.SemanticEffectTags;
	TemplateSeed->Metadata.TargetCategoryTags =
		CatalogSpec.TargetCategoryTags;
	TemplateSeed->Metadata.RequiredChannels =
		Validation.RequiredChannels;
	TemplateSeed->Metadata.BlockedStates = {
		TEXT("dead"),
		TEXT("ragdoll"),
		TEXT("stunned")
	};
	for (const FLLMNPCMotionRecipePrimitive& Primitive :
		Recipe.Primitives)
	{
		const FLLMNPCMotionPrimitiveDefinition* Definition =
			Registry.Find(Primitive.PrimitiveId);
		TemplateSeed->Metadata.RequiredCapabilities.AddUnique(
			Primitive.PrimitiveId
		);
		if (!Definition)
		{
			continue;
		}
		for (const FName CapabilityId :
			Definition->RequiredCapabilities)
		{
			TemplateSeed->Metadata.RequiredCapabilities.AddUnique(
				CapabilityId
			);
		}
		for (const FName BlockedState : Definition->BlockedStates)
		{
			if (
				(
					BlockedState == TEXT("left_hand_busy") &&
					Primitive.Side == TEXT("right")
				) ||
				(
					BlockedState == TEXT("right_hand_busy") &&
					Primitive.Side == TEXT("left")
				)
			)
			{
				continue;
			}
			TemplateSeed->Metadata.BlockedStates.AddUnique(BlockedState);
		}
	}
	TemplateSeed->Metadata.RequiredCapabilities.Sort(
		FNameLexicalLess()
	);
	TemplateSeed->Metadata.RequiredChannels.Sort(FNameLexicalLess());
	TemplateSeed->Metadata.BlockedStates.Sort(FNameLexicalLess());
	TemplateSeed->Metadata.SkeletonProfileId = SkeletonProfileId;
	TemplateSeed->Metadata.bRequiresTarget =
		AuthoringContract->bTargetRequired;
	TemplateSeed->Metadata.bCanRunWhileMoving =
		CatalogSpec.bCanRunWhileMoving;
	TemplateSeed->Metadata.bAllowRuntimeModelSelection = true;
	TemplateSeed->Metadata.CooldownSeconds = 1.0f;
	TemplateSeed->Metadata.Expressiveness =
		FMath::Clamp(CatalogSpec.Expressiveness, 0.0f, 1.0f);
	TemplateSeed->Metadata.Energy =
		FMath::Clamp(CatalogSpec.Energy, 0.0f, 1.0f);
	TemplateSeed->Metadata.SocialIntensity =
		FMath::Clamp(CatalogSpec.SocialIntensity, 0.0f, 1.0f);
	if (
		Evidence.AuthoringContractId ==
			LLMNPCMotionRecipeAuthoring::
				ProceduralClapAuthoringContractId
	)
	{
		TemplateSeed->Metadata.VariantDifference =
			TEXT("Procedural hands.contact variant for comparison with the reviewed AnimationAsset baseline.");
	}
	else if (
		Evidence.AuthoringContractId ==
			LLMNPCMotionRecipeAuthoring::
				ProceduralBeckonAuthoringContractId
	)
	{
		TemplateSeed->Metadata.VariantDifference =
			TEXT("Target-following Manny beckon with constrained palm-up orientation, bounded finger curl cycles, and occupied-hand mirroring.");
	}
	else if (
		Evidence.AuthoringContractId ==
			LLMNPCMotionRecipeAuthoring::
				ProceduralPresentAuthoringContractId
	)
	{
		TemplateSeed->Metadata.VariantDifference =
			TEXT("Target-following Manny open-palm presentation with bounded reach, explicit palm-up orientation, calibrated open fingers, and occupied-hand mirroring.");
	}
	else if (
		Evidence.AuthoringContractId ==
			LLMNPCMotionRecipeAuthoring::
				ProceduralThumbsUpAuthoringContractId
	)
	{
		TemplateSeed->Metadata.VariantDifference =
			TEXT("Target-independent Manny thumbs-up with a bounded upper-chest arm anchor, constrained outward wrist, calibrated thumb extension, four-finger curl, and occupied-hand mirroring.");
	}
	else
	{
		TemplateSeed->Metadata.VariantDifference =
			TEXT("Online model-authored Motion Recipe compiled for the Manny capability profile.");
	}
	TemplateSeed->Metadata.SourceRecipeHash =
		CompiledMetadata.RecipeHash;
	TemplateSeed->Metadata.KinematicReportHash =
		KinematicReport.ReportHash;
	TemplateSeed->Metadata.ReviewState =
		ELLMNPCTemplateReviewState::Generated;

	TemplateSeed->ModifierPolicy.PolicyVersion = 2;
	TemplateSeed->ModifierPolicy.AmplitudeRange =
		FVector2D(0.85f, 1.15f);
	TemplateSeed->ModifierPolicy.SpeedRange =
		FVector2D(0.9f, 1.1f);
	TemplateSeed->ModifierPolicy.DurationRange =
		FVector2D(0.95f, 1.05f);
	TemplateSeed->ModifierPolicy.bAllowMirror =
		AuthoringContract->bAllowMirror;
	TemplateSeed->ModifierPolicy.AllowedStyleTags =
		CatalogSpec.VariantStyleTags;
	if (AuthoringContract->bTargetRequired)
	{
		TemplateSeed->ModifierPolicy.ReachScaleRange =
			FVector2D(0.8f, 1.15f);
		TemplateSeed->ModifierPolicy.HeightScaleRange =
			FVector2D(0.8f, 1.12f);
		TemplateSeed->ModifierPolicy.LateralScaleRange =
			FVector2D(0.85f, 1.0f);
		TemplateSeed->ModifierPolicy.PalmOrientationWeightRange =
			FVector2D(0.85f, 1.0f);
		TemplateSeed->ModifierPolicy.FingerPoseWeightRange =
			FVector2D(0.85f, 1.0f);
		TemplateSeed->ModifierPolicy.bEnableDynamicTargetTracking =
			true;
		TemplateSeed->ModifierPolicy.TargetLostFadeSeconds = 0.18f;
		TemplateSeed->ModifierPolicy.TargetLossPolicy =
			ELLMNPCTargetLossPolicy::FadeOut;
		TemplateSeed->ModifierPolicy.bEnableObstacleAdaptation =
			true;
	}
	TemplateSeed->ModifierPolicy.TorsoParticipationRange =
		FVector2D(0.8f, 1.0f);
	TemplateSeed->ModifierPolicy.RandomAmplitudeJitter = 0.015f;
	TemplateSeed->ModifierPolicy.RandomSpeedJitter = 0.015f;
	TemplateSeed->ModifierPolicy.RandomFrequencyJitter = 0.0f;
	TemplateSeed->ModifierPolicy.RandomPhaseJitterRadians = 0.0f;
	TemplateSeed->ProceduralClip = Plan.Clip;
	if (!SerializeJsonObject(
		Provenance,
		TemplateSeed->SourceProvenanceJson
	))
	{
		return ErrorResult(
			TEXT("LLMNPC_RECIPE_DRAFT_PROVENANCE_SERIALIZE_FAILED"),
			TEXT("Motion Recipe provenance could not be serialized.")
		);
	}
	FString TemplateValidationError;
	if (!TemplateSeed->ValidateTemplate(TemplateValidationError))
	{
		return ErrorResult(
			FName(*TemplateValidationError),
			TemplateValidationError
		);
	}
	const ULLMNPCSettings* Settings =
		GetDefault<ULLMNPCSettings>();
	ULLMNPCActionVocabulary* Vocabulary =
		Settings
			? Settings->ActionVocabulary.LoadSynchronous()
			: nullptr;
	FString VocabularyError;
	if (
		!Vocabulary ||
		!Vocabulary->ValidateVocabulary(VocabularyError)
	)
	{
		return ErrorResult(
			VocabularyError.IsEmpty()
				? TEXT("LLMNPC_RECIPE_DRAFT_VOCABULARY_MISSING")
				: FName(*VocabularyError),
			VocabularyError.IsEmpty()
				? TEXT("The Action Vocabulary is unavailable.")
				: VocabularyError
		);
	}
	if (!ValidateTemplateVocabularyTags(
		*TemplateSeed,
		*Vocabulary,
		VocabularyError
	))
	{
		return ErrorResult(FName(*VocabularyError), VocabularyError);
	}

	ULLMNPCPublicActionDefinition* Definition =
		FindPublicActionDefinition(CatalogSpec.PublicActionId);
	TObjectPtr<ULLMNPCPublicActionDefinition> DefinitionSeed;
	if (Definition)
	{
		if (
			Definition->bRequiresTarget !=
				AuthoringContract->bTargetRequired ||
			Definition->TargetCategoryTags !=
				CatalogSpec.TargetCategoryTags
		)
		{
			return ErrorResult(
				TEXT("LLMNPC_RECIPE_DRAFT_PUBLIC_ACTION_CONFLICT"),
				TEXT("An existing Public Action has an incompatible target contract.")
			);
		}
	}
	else
	{
		DefinitionSeed =
			NewObject<ULLMNPCPublicActionDefinition>(
				GetTransientPackage()
			);
		DefinitionSeed->PublicActionId =
			CatalogSpec.PublicActionId;
		DefinitionSeed->SemanticVersion =
			CatalogSpec.SemanticVersion;
		DefinitionSeed->DefinitionRevision = 1;
		DefinitionSeed->DisplayName =
			FText::FromString(CatalogSpec.DisplayName);
		DefinitionSeed->SelectionSummary =
			CatalogSpec.SelectionSummary;
		DefinitionSeed->SuitableWhen = CatalogSpec.SuitableWhen;
		DefinitionSeed->AvoidWhen = CatalogSpec.AvoidWhen;
		DefinitionSeed->SemanticEffectTags =
			CatalogSpec.SemanticEffectTags;
		DefinitionSeed->TargetCategoryTags =
			CatalogSpec.TargetCategoryTags;
		DefinitionSeed->GestureFamily =
			CatalogSpec.GestureFamily;
		DefinitionSeed->DefaultStyle =
			CatalogSpec.DefaultStyle;
		DefinitionSeed->SearchKeywords =
			CatalogSpec.SearchKeywords;
		DefinitionSeed->bRequiresTarget =
			AuthoringContract->bTargetRequired;
		DefinitionSeed->CatalogSchemaVersion =
			LLMNPCCatalog::PublicActionSchemaVersion;
		DefinitionSeed->ReviewState =
			ELLMNPCTemplateReviewState::Generated;
		TSharedRef<FJsonObject> DefinitionReview =
			MakeShared<FJsonObject>();
		DefinitionReview->SetStringField(
			TEXT("schema_version"),
			TEXT("llmnpc.public_action_review.v1")
		);
		DefinitionReview->SetStringField(
			TEXT("authoring_bundle_id"),
			CompiledMetadata.CompiledRecipeHash
		);
		DefinitionReview->SetStringField(
			TEXT("source_recipe_hash"),
			CompiledMetadata.RecipeHash
		);
		SerializeJsonObject(
			DefinitionReview,
			DefinitionSeed->ReviewRecordJson
		);
		DefinitionSeed->ContentHash =
			ULLMNPCPublicActionDefinition::BuildContentHash(
				*DefinitionSeed
			);
		FString DefinitionError;
		if (
			!DefinitionSeed->ValidateDefinition(
				Vocabulary,
				DefinitionError
			)
		)
		{
			return ErrorResult(
				DefinitionError.IsEmpty()
					? TEXT("LLMNPC_RECIPE_DRAFT_VOCABULARY_MISSING")
					: FName(*DefinitionError),
				DefinitionError.IsEmpty()
					? TEXT("The Action Vocabulary is unavailable.")
					: DefinitionError
			);
		}
	}

	ULLMNPCMotionTemplate* TemplateAsset = nullptr;
	if (!CreateTemplateAsset(
		DestinationPackagePath,
		CatalogSpec.AssetName,
		*TemplateSeed,
		TemplateAsset,
		Error
	))
	{
		return ErrorResult(FName(*Error), Error);
	}
	if (!SaveTemplateAsset(TemplateAsset, Error))
	{
		return ErrorResult(FName(*Error), Error);
	}

	if (DefinitionSeed)
	{
		if (!CreatePublicActionAsset(
			PublicActionDraftDestinationPath,
			CatalogSpec.PublicActionAssetName,
			*DefinitionSeed,
			Definition,
			Error
		))
		{
			return ErrorResult(FName(*Error), Error);
		}
		if (!SavePublicActionAsset(Definition, Error))
		{
			return ErrorResult(FName(*Error), Error);
		}
	}

	FLLMNPCAuthoringOperationResult Result = SuccessResult(
		Definition && Definition->IsPublished()
			? TEXT("Online Motion Recipe compiled as a Generated template; its Public Action is already Published.")
			: TEXT("Online Motion Recipe compiled as a Generated template with a separate Generated Public Action draft. Neither is runtime-selectable."),
		TemplateAsset->GetPathName(),
		TemplateAsset
	);
	Result.PublicActionAsset = Definition;
	return Result;
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

	const ULLMNPCSettings* Settings =
		GetDefault<ULLMNPCSettings>();
	ULLMNPCActionVocabulary* Vocabulary =
		Settings
			? Settings->ActionVocabulary.LoadSynchronous()
			: nullptr;
	FString VocabularyError;
	const bool bVocabularyValid =
		Vocabulary &&
		Vocabulary->ValidateVocabulary(VocabularyError);
	AddCheck(
		TEXT("action_vocabulary"),
		bVocabularyValid,
		bVocabularyValid
			? TEXT("Action Vocabulary exists and validates.")
			: (
				VocabularyError.IsEmpty()
					? TEXT("Action Vocabulary is unavailable.")
					: VocabularyError
			)
	);
	FString TemplateVocabularyError;
	const bool bTemplateVocabularyValid =
		bVocabularyValid &&
		ValidateTemplateVocabularyTags(
			*Template,
			*Vocabulary,
			TemplateVocabularyError
		);
	AddCheck(
		TEXT("template_vocabulary_tags"),
		bTemplateVocabularyValid,
		bTemplateVocabularyValid
			? TEXT("Template catalog tags use the controlled Action Vocabulary.")
			: (
				TemplateVocabularyError.IsEmpty()
					? TEXT("Template tags could not be validated.")
					: TemplateVocabularyError
			)
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
	const bool bMotionRecipeTemplate =
		!bAnimationTemplate &&
		!Template->Metadata.SourceRecipeHash.TrimStartAndEnd().IsEmpty();
	FLLMNPCRecipeQualityIdentity RecipeIdentity;
	if (bMotionRecipeTemplate)
	{
		FString RecipeEvidenceError;
		const bool bRecipeEvidenceValid =
			bProvenanceJsonValid &&
			ValidateMotionRecipeEvidenceEnvelope(
				Provenance,
				RecipeEvidenceError
			);
		AddCheck(
			TEXT("motion_recipe_provenance"),
			bRecipeEvidenceValid,
			bRecipeEvidenceValid
				? TEXT("Online Motion Recipe Job, provider identity, prompt, license, and sanitized source evidence validate.")
				: (
					RecipeEvidenceError.IsEmpty()
						? TEXT("Motion Recipe provenance is invalid.")
						: RecipeEvidenceError
				)
		);
		FString RecipeCompileError;
		const bool bRecipeRecompiled =
			bRecipeEvidenceValid &&
			RecompileMotionRecipeTemplate(
				*Template,
				Provenance,
				RecipeIdentity,
				RecipeCompileError
			);
		AddCheck(
			TEXT("motion_recipe_recompile"),
			bRecipeRecompiled,
			bRecipeRecompiled
				? TEXT("Recipe recompiles deterministically to the stored Motion Clip under the current Manny Capability.")
				: (
					RecipeCompileError.IsEmpty()
						? TEXT("Motion Recipe could not be deterministically recompiled.")
						: RecipeCompileError
				)
		);
	}
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
	else if (bMotionRecipeTemplate)
	{
		if (bReconstructionRequested)
		{
			Warnings.Add(MakeShared<FJsonValueString>(
				TEXT("Reconstruction Profile input is ignored for a Motion Recipe Draft; deterministic Recipe recompilation is authoritative.")
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
	if (bMotionRecipeTemplate)
	{
		if (!FullPoseArtifactFilePath.TrimStartAndEnd().IsEmpty())
		{
			Warnings.Add(MakeShared<FJsonValueString>(
				TEXT("Full Pose Artifact input is ignored for a Motion Recipe Draft.")
			));
		}
	}
	else if (!FullPoseArtifactFilePath.TrimStartAndEnd().IsEmpty())
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
	Report->SetStringField(
		TEXT("source_type"),
		bMotionRecipeTemplate
			? TEXT("motion_recipe")
			: bAnimationTemplate
				? TEXT("animation_asset")
				: TEXT("reconstruction_profile")
	);
	Report->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());
	Report->SetStringField(TEXT("source_reconstruction_hash"), Summary.ProfileContentHash);
	Report->SetStringField(TEXT("full_pose_validation"), FullPoseValidation);
	if (bMotionRecipeTemplate)
	{
		Report->SetStringField(
			TEXT("source_recipe_hash"),
			RecipeIdentity.RecipeHash
		);
		Report->SetStringField(
			TEXT("compiled_recipe_hash"),
			RecipeIdentity.CompiledRecipeHash
		);
		Report->SetStringField(
			TEXT("capability_hash"),
			RecipeIdentity.CapabilityHash
		);
		Report->SetStringField(
			TEXT("primitive_registry_version"),
			RecipeIdentity.RegistryVersion
		);
		Report->SetStringField(
			TEXT("compiler_version"),
			RecipeIdentity.CompilerVersion
		);
	}
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
	if (Template->Metadata.bRequiresTarget)
	{
		AActor* PreviewTarget = UGameplayStatics::GetPlayerPawn(
			PreviewActor,
			0
		);
		if (
			!IsValid(PreviewTarget) ||
			PreviewTarget == PreviewActor
		)
		{
			return ErrorResult(
				TEXT("LLMNPC_AUTHORING_PREVIEW_TARGET_UNAVAILABLE"),
				TEXT("Targeted template preview requires player 0 to be a different PIE Actor from the selected Manny.")
			);
		}
		MotionComponent->RegisterTarget(
			TEXT("authoring_preview_target"),
			PreviewTarget
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

FString ULLMNPCTemplateAuthoringSubsystem::
	GetPublishedPublicActionSourceDirectory()
{
	const TSharedPtr<IPlugin> Plugin =
		IPluginManager::Get().FindPlugin(TEXT("LLMNPCActionLayer"));
	if (Plugin.IsValid() && !Plugin->GetDescriptor().bInstalled)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(
			Plugin->GetBaseDir(),
			TEXT("Resources/PublicActions/Published")
		));
	}
	return FPaths::ConvertRelativePathToFull(FPaths::Combine(
		FPaths::ProjectDir(),
		TEXT("LLMNPCSource/PublicActions/Published")
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
	PreviewCopy->Metadata.CatalogContentHash.Reset();
	PreviewCopy->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(*PreviewCopy);
	FLLMNPCTemplateModifiers Modifiers;
	if (Template.Metadata.bRequiresTarget)
	{
		Modifiers.TargetRef =
			TEXT("authoring_preview_target");
	}
	return FLLMNPCTemplateCompiler::Compile(
		*PreviewCopy,
		Modifiers,
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
		GetPublishedPublicActionSourceDirectory()
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
	const FString Directory = GetPublishedPublicActionSourceDirectory();
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
	StableMetadata.CatalogContentHash.Reset();
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
	else if (!Template.Metadata.SourceRecipeHash.TrimStartAndEnd().IsEmpty())
	{
		TSharedPtr<FJsonObject> Provenance;
		FLLMNPCRecipeQualityIdentity Identity;
		FString EvidenceError;
		FString RecompileError;
		FString ReportSourceType;
		FString ReportRecipeHash;
		FString ReportCompiledHash;
		FString ReportCapabilityHash;
		FString ReportRegistryVersion;
		FString ReportCompilerVersion;
		if (
			!ParseJsonObject(
				Template.SourceProvenanceJson,
				Provenance
			) ||
			!ValidateMotionRecipeEvidenceEnvelope(
				Provenance,
				EvidenceError
			) ||
			!RecompileMotionRecipeTemplate(
				Template,
				Provenance,
				Identity,
				RecompileError
			) ||
			!Report->TryGetStringField(
				TEXT("source_type"),
				ReportSourceType
			) ||
			ReportSourceType != TEXT("motion_recipe") ||
			!Report->TryGetStringField(
				TEXT("source_recipe_hash"),
				ReportRecipeHash
			) ||
			ReportRecipeHash != Identity.RecipeHash ||
			!Report->TryGetStringField(
				TEXT("compiled_recipe_hash"),
				ReportCompiledHash
			) ||
			ReportCompiledHash != Identity.CompiledRecipeHash ||
			!Report->TryGetStringField(
				TEXT("capability_hash"),
				ReportCapabilityHash
			) ||
			ReportCapabilityHash != Identity.CapabilityHash ||
			!Report->TryGetStringField(
				TEXT("primitive_registry_version"),
				ReportRegistryVersion
			) ||
			ReportRegistryVersion != Identity.RegistryVersion ||
			!Report->TryGetStringField(
				TEXT("compiler_version"),
				ReportCompilerVersion
			) ||
			ReportCompilerVersion != Identity.CompilerVersion
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
