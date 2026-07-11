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

bool IsValidAnimationPlaybackPolicy(const FLLMNPCAnimationPlaybackPolicy& Policy)
{
	return
		!Policy.SlotName.IsNone() &&
		FMath::IsFinite(Policy.BlendInSeconds) &&
		FMath::IsFinite(Policy.BlendOutSeconds) &&
		FMath::IsFinite(Policy.StartPositionSeconds) &&
		FMath::IsFinite(Policy.MaxDurationSeconds) &&
		Policy.BlendInSeconds >= 0.0f &&
		Policy.BlendInSeconds <= 2.0f &&
		Policy.BlendOutSeconds >= 0.0f &&
		Policy.BlendOutSeconds <= 2.0f &&
		Policy.StartPositionSeconds >= 0.0f &&
		Policy.MaxDurationSeconds >= 0.1f &&
		Policy.MaxDurationSeconds <= 60.0f;
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
	if (Metadata.VariantId.IsNone() || !FMath::IsFinite(Metadata.VariantWeight) || Metadata.VariantWeight <= 0.0f)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_VARIANT_INVALID");
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
	if (
		!FMath::IsFinite(ModifierPolicy.RandomAmplitudeJitter) ||
		!FMath::IsFinite(ModifierPolicy.RandomSpeedJitter) ||
		!FMath::IsFinite(ModifierPolicy.RandomFrequencyJitter) ||
		!FMath::IsFinite(ModifierPolicy.RandomPhaseJitterRadians) ||
		ModifierPolicy.RandomAmplitudeJitter < 0.0f ||
		ModifierPolicy.RandomAmplitudeJitter > 0.25f ||
		ModifierPolicy.RandomSpeedJitter < 0.0f ||
		ModifierPolicy.RandomSpeedJitter > 0.25f ||
		ModifierPolicy.RandomFrequencyJitter < 0.0f ||
		ModifierPolicy.RandomFrequencyJitter > 0.25f ||
		ModifierPolicy.RandomPhaseJitterRadians < 0.0f ||
		ModifierPolicy.RandomPhaseJitterRadians > 0.5f
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_RANDOMIZATION_POLICY_INVALID");
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
	else if (Kind == ELLMNPCTemplateKind::AnimationAsset)
	{
		if (AnimationAsset.IsNull())
		{
			OutError = TEXT("LLMNPC_TEMPLATE_ANIMATION_ASSET_MISSING");
			return false;
		}
		if (Metadata.RequiredChannels.IsEmpty())
		{
			OutError = TEXT("LLMNPC_TEMPLATE_ANIMATION_CHANNELS_MISSING");
			return false;
		}
		if (!IsValidAnimationPlaybackPolicy(AnimationPlayback))
		{
			OutError = TEXT("LLMNPC_TEMPLATE_ANIMATION_PLAYBACK_POLICY_INVALID");
			return false;
		}
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
