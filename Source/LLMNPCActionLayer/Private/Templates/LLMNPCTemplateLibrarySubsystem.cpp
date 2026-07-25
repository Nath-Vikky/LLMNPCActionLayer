#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

#include "Engine/AssetManager.h"
#include "LLMNPCActionLayer.h"
#include "LLMNPCSettings.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCActionVocabulary.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"

namespace
{
const FPrimaryAssetType TemplateAssetType(TEXT("LLMNPCTemplate"));
const FPrimaryAssetType PublicActionAssetType(TEXT("LLMNPCPublicAction"));
const FPrimaryAssetType SkeletonProfileAssetType(TEXT("LLMNPCSkeletonProfile"));

FString DiagnosticToString(const FLLMNPCCatalogDiagnostic& Diagnostic)
{
	return FString::Printf(
		TEXT("%s:%s:%s:%s"),
		*Diagnostic.Code.ToString(),
		*Diagnostic.AssetPath,
		*Diagnostic.FieldPath,
		*Diagnostic.Message
	);
}
}

void ULLMNPCTemplateLibrarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RefreshLibrary();
}

void ULLMNPCTemplateLibrarySubsystem::Deinitialize()
{
	LoadedTemplates.Reset();
	LoadedPublicActions.Reset();
	LoadedVocabulary = nullptr;
	SkeletonProfileIndex.Reset();
	CatalogIndex.Reset();
	ScanErrors.Reset();
	Super::Deinitialize();
}

void ULLMNPCTemplateLibrarySubsystem::RefreshLibrary()
{
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings)
	{
		ScanErrors = { TEXT("LLMNPC_TEMPLATE_LIBRARY_SETTINGS_MISSING") };
		return;
	}

	UAssetManager& AssetManager = UAssetManager::Get();
	TMap<FName, TObjectPtr<ULLMNPCSkeletonProfile>> NewProfiles;
	TArray<TObjectPtr<ULLMNPCMotionTemplate>> NewTemplates;
	TArray<TObjectPtr<ULLMNPCPublicActionDefinition>> NewDefinitions;
	TArray<FString> NewErrors;

	for (const FString& Path : Settings->SkeletonProfileScanPaths)
	{
		AssetManager.ScanPathForPrimaryAssets(
			SkeletonProfileAssetType,
			Path,
			ULLMNPCSkeletonProfile::StaticClass(),
			false
		);
	}
	TArray<FPrimaryAssetId> ProfileIds;
	AssetManager.GetPrimaryAssetIdList(SkeletonProfileAssetType, ProfileIds);
	for (const FPrimaryAssetId& AssetId : ProfileIds)
	{
		const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
		ULLMNPCSkeletonProfile* Profile =
			Cast<ULLMNPCSkeletonProfile>(AssetPath.TryLoad());
		FString Error;
		if (!Profile || !Profile->ValidateProfile(Error))
		{
			NewErrors.Add(FString::Printf(
				TEXT("LLMNPC_TEMPLATE_LIBRARY_PROFILE_INVALID:%s:%s"),
				*AssetPath.ToString(),
				*Error
			));
			continue;
		}
		if (NewProfiles.Contains(Profile->ProfileId))
		{
			NewErrors.Add(FString::Printf(
				TEXT("LLMNPC_TEMPLATE_LIBRARY_DUPLICATE_PROFILE_ID:%s"),
				*Profile->ProfileId.ToString()
			));
			continue;
		}
		NewProfiles.Add(Profile->ProfileId, Profile);
	}

	for (const FString& Path : Settings->MotionTemplateScanPaths)
	{
		AssetManager.ScanPathForPrimaryAssets(
			TemplateAssetType,
			Path,
			ULLMNPCMotionTemplate::StaticClass(),
			false
		);
	}
	TArray<FPrimaryAssetId> TemplateIds;
	AssetManager.GetPrimaryAssetIdList(TemplateAssetType, TemplateIds);
	for (const FPrimaryAssetId& AssetId : TemplateIds)
	{
		const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
		if (ULLMNPCMotionTemplate* Template =
			Cast<ULLMNPCMotionTemplate>(AssetPath.TryLoad()))
		{
			NewTemplates.AddUnique(Template);
		}
		else
		{
			NewErrors.Add(FString::Printf(
				TEXT("LLMNPC_TEMPLATE_LIBRARY_TEMPLATE_LOAD_FAILED:%s"),
				*AssetPath.ToString()
			));
		}
	}

	for (const FString& Path : Settings->PublicActionDefinitionScanPaths)
	{
		AssetManager.ScanPathForPrimaryAssets(
			PublicActionAssetType,
			Path,
			ULLMNPCPublicActionDefinition::StaticClass(),
			false
		);
	}
	TArray<FPrimaryAssetId> DefinitionIds;
	AssetManager.GetPrimaryAssetIdList(PublicActionAssetType, DefinitionIds);
	for (const FPrimaryAssetId& AssetId : DefinitionIds)
	{
		const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
		if (ULLMNPCPublicActionDefinition* Definition =
			Cast<ULLMNPCPublicActionDefinition>(AssetPath.TryLoad()))
		{
			NewDefinitions.AddUnique(Definition);
		}
		else
		{
			NewErrors.Add(FString::Printf(
				TEXT("LLMNPC_TEMPLATE_LIBRARY_PUBLIC_ACTION_LOAD_FAILED:%s"),
				*AssetPath.ToString()
			));
		}
	}

	ULLMNPCActionVocabulary* NewVocabulary =
		Cast<ULLMNPCActionVocabulary>(Settings->ActionVocabulary.LoadSynchronous());
	FLLMNPCTemplateSearchIndex NewIndex;
	TArray<ULLMNPCMotionTemplate*> RawTemplates;
	TArray<ULLMNPCPublicActionDefinition*> RawDefinitions;
	TSet<FName> ProfileNames;
	for (const TObjectPtr<ULLMNPCMotionTemplate>& Template : NewTemplates)
	{
		RawTemplates.Add(Template.Get());
	}
	for (const TObjectPtr<ULLMNPCPublicActionDefinition>& Definition : NewDefinitions)
	{
		RawDefinitions.Add(Definition.Get());
	}
	NewProfiles.GetKeys(ProfileNames);
	if (!NewIndex.Build(
		RawTemplates,
		RawDefinitions,
		NewVocabulary,
		ProfileNames
	))
	{
		for (const FLLMNPCCatalogDiagnostic& Diagnostic : NewIndex.GetDiagnostics())
		{
			NewErrors.Add(DiagnosticToString(Diagnostic));
		}
		ScanErrors = MoveTemp(NewErrors);
		return;
	}
	for (const FLLMNPCCatalogDiagnostic& Diagnostic : NewIndex.GetDiagnostics())
	{
		NewErrors.Add(DiagnosticToString(Diagnostic));
	}

	LoadedTemplates = MoveTemp(NewTemplates);
	LoadedPublicActions = MoveTemp(NewDefinitions);
	LoadedVocabulary = NewVocabulary;
	SkeletonProfileIndex = MoveTemp(NewProfiles);
	CatalogIndex = MoveTemp(NewIndex);
	ScanErrors = MoveTemp(NewErrors);
	for (const FString& ScanError : ScanErrors)
	{
		UE_LOG(
			LogLLMNPCActionLayer,
			Warning,
			TEXT("LLMNPCTemplateLibrary catalog diagnostic: %s"),
			*ScanError
		);
	}

	UE_LOG(
		LogLLMNPCActionLayer,
		Log,
		TEXT("LLMNPCTemplateLibrary: indexed %d templates, %d public actions, and %d skeleton profiles. Catalog=%s Errors=%d"),
		CatalogIndex.GetTemplateCount(),
		CatalogIndex.GetPublicActionCount(),
		SkeletonProfileIndex.Num(),
		*CatalogIndex.GetCatalogHash(),
		ScanErrors.Num()
	);
}

const ULLMNPCMotionTemplate* ULLMNPCTemplateLibrarySubsystem::FindPublishedTemplate(
	FName TemplateId
) const
{
	return CatalogIndex.FindTemplate(TemplateId);
}

const ULLMNPCPublicActionDefinition*
ULLMNPCTemplateLibrarySubsystem::FindPublishedPublicAction(
	FName PublicActionId
) const
{
	return CatalogIndex.FindDefinition(PublicActionId);
}

void ULLMNPCTemplateLibrarySubsystem::GetPublishedTemplateIdsForProfile(
	FName SkeletonProfileId,
	TArray<FName>& OutTemplateIds
) const
{
	OutTemplateIds.Reset();
	TArray<FName> TemplateIds;
	CatalogIndex.GetTemplateIds(TemplateIds);
	for (const FName TemplateId : TemplateIds)
	{
		const ULLMNPCMotionTemplate* Template =
			CatalogIndex.FindTemplate(TemplateId);
		if (Template && Template->SupportsSkeletonProfile(SkeletonProfileId))
		{
			OutTemplateIds.Add(TemplateId);
		}
	}
}

const ULLMNPCMotionTemplate* ULLMNPCTemplateLibrarySubsystem::FindPublishedVariant(
	FName PublicActionId,
	FName SkeletonProfileId
) const
{
	const ULLMNPCPublicActionDefinition* Definition =
		FindPublishedPublicAction(PublicActionId);
	return ResolvePublishedVariant(
		PublicActionId,
		SkeletonProfileId,
		Definition ? Definition->DefaultStyle : FName(TEXT("neutral")),
		0
	);
}

const ULLMNPCMotionTemplate* ULLMNPCTemplateLibrarySubsystem::ResolvePublishedVariant(
	FName PublicActionId,
	FName SkeletonProfileId,
	FName StyleTag,
	int32 RandomSeed
) const
{
	const TArray<FName>* TemplateIds = CatalogIndex.FindVariants(PublicActionId);
	if (!TemplateIds)
	{
		return nullptr;
	}

	TArray<const ULLMNPCMotionTemplate*> ExactStyleVariants;
	TArray<const ULLMNPCMotionTemplate*> CompatibleStyleVariants;
	for (const FName TemplateId : *TemplateIds)
	{
		const ULLMNPCMotionTemplate* Template = FindPublishedTemplate(TemplateId);
		if (!Template || !Template->SupportsSkeletonProfile(SkeletonProfileId))
		{
			continue;
		}
		TArray<FName> Styles = Template->Metadata.VariantStyleTags;
		if (Styles.IsEmpty())
		{
			Styles = Template->ModifierPolicy.AllowedStyleTags;
		}
		if (!Styles.Contains(StyleTag))
		{
			continue;
		}
		const bool bExactProfile =
			Template->Metadata.SkeletonProfileId == SkeletonProfileId;
		(bExactProfile ? ExactStyleVariants : CompatibleStyleVariants).Add(Template);
	}
	const TArray<const ULLMNPCMotionTemplate*>& Variants =
		!ExactStyleVariants.IsEmpty()
		? ExactStyleVariants
		: CompatibleStyleVariants;
	if (Variants.IsEmpty())
	{
		return nullptr;
	}

	float TotalWeight = 0.0f;
	for (const ULLMNPCMotionTemplate* Variant : Variants)
	{
		TotalWeight += Variant->Metadata.VariantWeight;
	}
	FRandomStream Stream(RandomSeed);
	float Choice = RandomSeed == 0
		? 0.0f
		: Stream.FRandRange(0.0f, TotalWeight);
	for (const ULLMNPCMotionTemplate* Variant : Variants)
	{
		Choice -= Variant->Metadata.VariantWeight;
		if (Choice <= 0.0f)
		{
			return Variant;
		}
	}
	return Variants.Last();
}

const ULLMNPCSkeletonProfile* ULLMNPCTemplateLibrarySubsystem::FindSkeletonProfile(
	FName ProfileId
) const
{
	const TObjectPtr<ULLMNPCSkeletonProfile>* Found =
		SkeletonProfileIndex.Find(ProfileId);
	return Found ? Found->Get() : nullptr;
}

const ULLMNPCMotionTemplate*
ULLMNPCTemplateLibrarySubsystem::ResolveRuntimeModelTemplate(
	FName SelectionId,
	FName SkeletonProfileId
) const
{
	if (SelectionId.IsNone() || SkeletonProfileId.IsNone())
	{
		return nullptr;
	}
	return FindPublishedVariant(SelectionId, SkeletonProfileId);
}

void ULLMNPCTemplateLibrarySubsystem::QueryRuntimeCandidates(
	FName SkeletonProfileId,
	TArray<FLLMNPCTemplateCandidate>& OutCandidates
) const
{
	OutCandidates.Reset();
	TArray<FName> PublicActionIds;
	CatalogIndex.GetPublicActionIds(PublicActionIds);
	for (const FName PublicActionId : PublicActionIds)
	{
		FLLMNPCTemplateCandidate Candidate;
		if (CatalogIndex.BuildRuntimeCandidate(PublicActionId, SkeletonProfileId, Candidate))
		{
			OutCandidates.Add(MoveTemp(Candidate));
		}
	}

	OutCandidates.Sort(
		[](const FLLMNPCTemplateCandidate& A, const FLLMNPCTemplateCandidate& B)
		{
			return A.SelectionId.LexicalLess(B.SelectionId);
		}
	);
}
