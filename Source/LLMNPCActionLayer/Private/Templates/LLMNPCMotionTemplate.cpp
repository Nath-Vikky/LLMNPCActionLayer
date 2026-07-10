#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
bool IsOrderedPositiveRange(const FVector2D& Range)
{
	return
		FMath::IsFinite(Range.X) &&
		FMath::IsFinite(Range.Y) &&
		Range.X > 0.0f &&
		Range.Y >= Range.X;
}
}

bool ULLMNPCMotionTemplate::IsPublished() const
{
	return Metadata.ReviewState == ELLMNPCTemplateReviewState::Published;
}

bool ULLMNPCMotionTemplate::ValidateTemplate(FString& OutError) const
{
	OutError.Reset();
	if (Metadata.TemplateId.IsNone())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_ID_MISSING");
		return false;
	}

	if (Metadata.PublicActionId.IsNone())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_PUBLIC_ACTION_ID_MISSING");
		return false;
	}

	if (Metadata.SemanticVersion.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_VERSION_MISSING");
		return false;
	}

	if (Metadata.SkeletonProfileId.IsNone())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_SKELETON_PROFILE_MISSING");
		return false;
	}

	if (
		!IsOrderedPositiveRange(ModifierPolicy.AmplitudeRange) ||
		!IsOrderedPositiveRange(ModifierPolicy.SpeedRange) ||
		!IsOrderedPositiveRange(ModifierPolicy.DurationRange)
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_MODIFIER_RANGE_INVALID");
		return false;
	}

	if (Kind == ELLMNPCTemplateKind::ProceduralMotion)
	{
		if (!FMath::IsFinite(ProceduralClip.Duration) || ProceduralClip.Duration <= 0.0f)
		{
			OutError = TEXT("LLMNPC_TEMPLATE_CLIP_DURATION_INVALID");
			return false;
		}

		if (ProceduralClip.Tracks.IsEmpty())
		{
			OutError = TEXT("LLMNPC_TEMPLATE_CLIP_TRACKS_EMPTY");
			return false;
		}
	}
	else if (Kind == ELLMNPCTemplateKind::AnimationAsset && AnimationAsset.IsNull())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_ANIMATION_ASSET_MISSING");
		return false;
	}

	if (IsPublished() && SourceProvenanceJson.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_PROVENANCE_MISSING");
		return false;
	}

	if (IsPublished() && ValidationReportJson.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_VALIDATION_REPORT_MISSING");
		return false;
	}

	return true;
}

FPrimaryAssetId ULLMNPCMotionTemplate::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(
		TEXT("LLMNPCTemplate"),
		Metadata.TemplateId.IsNone() ? GetFName() : Metadata.TemplateId
	);
}
