#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

#include "Engine/AssetManager.h"
#include "LLMNPCActionLayer.h"
#include "LLMNPCSettings.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
const FPrimaryAssetType TemplateAssetType(TEXT("LLMNPCTemplate"));
const FPrimaryAssetType SkeletonProfileAssetType(TEXT("LLMNPCSkeletonProfile"));
}

void ULLMNPCTemplateLibrarySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RefreshLibrary();
}

void ULLMNPCTemplateLibrarySubsystem::Deinitialize()
{
	TemplateIndex.Reset();
	SkeletonProfileIndex.Reset();
	PublicActionIndex.Reset();
	ScanErrors.Reset();
	Super::Deinitialize();
}

void ULLMNPCTemplateLibrarySubsystem::RefreshLibrary()
{
	TemplateIndex.Reset();
	SkeletonProfileIndex.Reset();
	PublicActionIndex.Reset();
	ScanErrors.Reset();

	UAssetManager& AssetManager = UAssetManager::Get();
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings)
	{
		ScanErrors.Add(TEXT("LLMNPC_TEMPLATE_LIBRARY_SETTINGS_MISSING"));
		return;
	}

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
		ULLMNPCSkeletonProfile* Profile = Cast<ULLMNPCSkeletonProfile>(AssetPath.TryLoad());
		if (!Profile)
		{
			ScanErrors.Add(FString::Printf(
				TEXT("LLMNPC_TEMPLATE_LIBRARY_PROFILE_LOAD_FAILED:%s"),
				*AssetPath.ToString()
			));
			continue;
		}

		FString ValidationError;
		if (!Profile->ValidateProfile(ValidationError))
		{
			ScanErrors.Add(ValidationError);
			continue;
		}

		if (SkeletonProfileIndex.Contains(Profile->ProfileId))
		{
			ScanErrors.Add(FString::Printf(
				TEXT("LLMNPC_TEMPLATE_LIBRARY_DUPLICATE_PROFILE_ID:%s"),
				*Profile->ProfileId.ToString()
			));
			continue;
		}

		SkeletonProfileIndex.Add(Profile->ProfileId, Profile);
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
		ULLMNPCMotionTemplate* MotionTemplate = Cast<ULLMNPCMotionTemplate>(AssetPath.TryLoad());
		if (!MotionTemplate)
		{
			ScanErrors.Add(FString::Printf(
				TEXT("LLMNPC_TEMPLATE_LIBRARY_TEMPLATE_LOAD_FAILED:%s"),
				*AssetPath.ToString()
			));
			continue;
		}

		FString ValidationError;
		if (!MotionTemplate->ValidateTemplate(ValidationError))
		{
			ScanErrors.Add(ValidationError);
			continue;
		}

		if (!MotionTemplate->IsPublished())
		{
			continue;
		}

		if (!SkeletonProfileIndex.Contains(MotionTemplate->Metadata.SkeletonProfileId))
		{
			ScanErrors.Add(FString::Printf(
				TEXT("LLMNPC_TEMPLATE_LIBRARY_PROFILE_NOT_FOUND:%s"),
				*MotionTemplate->Metadata.SkeletonProfileId.ToString()
			));
			continue;
		}

		if (TemplateIndex.Contains(MotionTemplate->Metadata.TemplateId))
		{
			ScanErrors.Add(FString::Printf(
				TEXT("LLMNPC_TEMPLATE_LIBRARY_DUPLICATE_TEMPLATE_ID:%s"),
				*MotionTemplate->Metadata.TemplateId.ToString()
			));
			continue;
		}

		TemplateIndex.Add(MotionTemplate->Metadata.TemplateId, MotionTemplate);
		if (MotionTemplate->Metadata.bAllowRuntimeModelSelection)
		{
			PublicActionIndex.FindOrAdd(MotionTemplate->Metadata.PublicActionId).Add(
				MotionTemplate->Metadata.TemplateId
			);
		}
	}

	UE_LOG(
		LogLLMNPCActionLayer,
		Log,
		TEXT("LLMNPCTemplateLibrary: indexed %d published templates and %d skeleton profiles (%d errors)."),
		TemplateIndex.Num(),
		SkeletonProfileIndex.Num(),
		ScanErrors.Num()
	);
}

const ULLMNPCMotionTemplate* ULLMNPCTemplateLibrarySubsystem::FindPublishedTemplate(FName TemplateId) const
{
	const TObjectPtr<ULLMNPCMotionTemplate>* Found = TemplateIndex.Find(TemplateId);
	return Found ? Found->Get() : nullptr;
}

const ULLMNPCMotionTemplate* ULLMNPCTemplateLibrarySubsystem::FindPublishedVariant(
	FName PublicActionId,
	FName SkeletonProfileId
) const
{
	const TArray<FName>* TemplateIds = PublicActionIndex.Find(PublicActionId);
	if (!TemplateIds)
	{
		return nullptr;
	}

	for (const FName TemplateId : *TemplateIds)
	{
		const ULLMNPCMotionTemplate* MotionTemplate = FindPublishedTemplate(TemplateId);
		if (MotionTemplate && MotionTemplate->Metadata.SkeletonProfileId == SkeletonProfileId)
		{
			return MotionTemplate;
		}
	}

	return nullptr;
}

const ULLMNPCSkeletonProfile* ULLMNPCTemplateLibrarySubsystem::FindSkeletonProfile(FName ProfileId) const
{
	const TObjectPtr<ULLMNPCSkeletonProfile>* Found = SkeletonProfileIndex.Find(ProfileId);
	return Found ? Found->Get() : nullptr;
}

const ULLMNPCMotionTemplate* ULLMNPCTemplateLibrarySubsystem::ResolveRuntimeModelTemplate(
	FName SelectionId,
	FName SkeletonProfileId
) const
{
	if (SelectionId.IsNone() || SkeletonProfileId.IsNone())
	{
		return nullptr;
	}

	if (const ULLMNPCMotionTemplate* ExactTemplate = FindPublishedTemplate(SelectionId))
	{
		if (
			ExactTemplate->Metadata.bAllowRuntimeModelSelection &&
			ExactTemplate->Metadata.SkeletonProfileId == SkeletonProfileId
		)
		{
			return ExactTemplate;
		}
	}

	return FindPublishedVariant(SelectionId, SkeletonProfileId);
}

void ULLMNPCTemplateLibrarySubsystem::QueryRuntimeCandidates(
	FName SkeletonProfileId,
	TArray<FLLMNPCTemplateCandidate>& OutCandidates
) const
{
	OutCandidates.Reset();
	for (const TPair<FName, TArray<FName>>& Pair : PublicActionIndex)
	{
		const ULLMNPCMotionTemplate* MotionTemplate = FindPublishedVariant(
			Pair.Key,
			SkeletonProfileId
		);
		if (!MotionTemplate)
		{
			continue;
		}

		FLLMNPCTemplateCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
		Candidate.SelectionId = MotionTemplate->Metadata.PublicActionId;
		Candidate.Description = MotionTemplate->Metadata.Description;
		Candidate.IntentTags = MotionTemplate->Metadata.IntentTags;
		Candidate.EmotionTags = MotionTemplate->Metadata.EmotionTags;
		Candidate.bRequiresTarget = MotionTemplate->Metadata.bRequiresTarget;
		Candidate.AmplitudeRange = MotionTemplate->ModifierPolicy.AmplitudeRange;
		Candidate.SpeedRange = MotionTemplate->ModifierPolicy.SpeedRange;
		Candidate.DurationRange = MotionTemplate->ModifierPolicy.DurationRange;
		Candidate.AllowedStyles = MotionTemplate->ModifierPolicy.AllowedStyleTags;
	}

	OutCandidates.Sort(
		[](const FLLMNPCTemplateCandidate& A, const FLLMNPCTemplateCandidate& B)
		{
			return A.SelectionId.LexicalLess(B.SelectionId);
		}
	);
}
