#include "Templates/LLMNPCMotionTemplate.h"

#include "Misc/SecureHash.h"

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

bool IsOrderedNonNegativeRange(const FVector2D& Range)
{
	return
		FMath::IsFinite(Range.X) &&
		FMath::IsFinite(Range.Y) &&
		Range.X >= 0.0f &&
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

FString JoinNames(const TArray<FName>& Names)
{
	TArray<FString> Values;
	for (const FName Name : Names)
	{
		Values.Add(Name.ToString().ToLower());
	}
	Values.Sort();
	return FString::Join(Values, TEXT(","));
}

bool HasUniqueNonEmptyNames(const TArray<FName>& Names)
{
	TSet<FName> Unique;
	for (const FName Name : Names)
	{
		if (Name.IsNone() || Unique.Contains(Name))
		{
			return false;
		}
		Unique.Add(Name);
	}
	return true;
}
}

bool ULLMNPCMotionTemplate::IsPublished() const
{
	return Metadata.ReviewState == ELLMNPCTemplateReviewState::Published;
}

bool ULLMNPCMotionTemplate::SupportsSkeletonProfile(FName ProfileId) const
{
	return !ProfileId.IsNone() &&
		(Metadata.SkeletonProfileId == ProfileId || Metadata.CompatibleSkeletonProfileIds.Contains(ProfileId));
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
	TSet<FName> UniqueCompatibleProfiles;
	for (const FName CompatibleProfileId : Metadata.CompatibleSkeletonProfileIds)
	{
		if (CompatibleProfileId.IsNone() ||
			CompatibleProfileId == Metadata.SkeletonProfileId ||
			UniqueCompatibleProfiles.Contains(CompatibleProfileId))
		{
			OutError = TEXT("LLMNPC_TEMPLATE_COMPATIBLE_SKELETON_PROFILE_INVALID");
			return false;
		}
		UniqueCompatibleProfiles.Add(CompatibleProfileId);
	}

	if (
		!HasUniqueNonEmptyNames(Metadata.VariantStyleTags) ||
		!HasUniqueNonEmptyNames(Metadata.IntentTags) ||
		!HasUniqueNonEmptyNames(Metadata.EmotionTags) ||
		!HasUniqueNonEmptyNames(Metadata.PersonalityTags) ||
		!HasUniqueNonEmptyNames(Metadata.RequiredChannels) ||
		!HasUniqueNonEmptyNames(Metadata.BlockedStates)
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_METADATA_TAGS_INVALID");
		return false;
	}

	if (
		ModifierPolicy.PolicyVersion < 1 ||
		ModifierPolicy.PolicyVersion > 2 ||
		!IsOrderedPositiveRange(ModifierPolicy.AmplitudeRange) ||
		!IsOrderedPositiveRange(ModifierPolicy.SpeedRange) ||
		!IsOrderedPositiveRange(ModifierPolicy.DurationRange) ||
		!IsOrderedPositiveRange(ModifierPolicy.ReachScaleRange) ||
		!IsOrderedPositiveRange(ModifierPolicy.HeightScaleRange) ||
		!IsOrderedPositiveRange(ModifierPolicy.LateralScaleRange) ||
		ModifierPolicy.CycleCountRange.X < 0 ||
		ModifierPolicy.CycleCountRange.Y < ModifierPolicy.CycleCountRange.X ||
		!IsOrderedNonNegativeRange(ModifierPolicy.GazeEngagementRange) ||
		!IsOrderedNonNegativeRange(ModifierPolicy.PalmOrientationWeightRange) ||
		!IsOrderedNonNegativeRange(ModifierPolicy.FingerPoseWeightRange) ||
		!IsOrderedNonNegativeRange(ModifierPolicy.TorsoParticipationRange) ||
		!IsOrderedPositiveRange(ModifierPolicy.BlendInScaleRange) ||
		!IsOrderedPositiveRange(ModifierPolicy.BlendOutScaleRange)
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_MODIFIER_RANGE_INVALID");
		return false;
	}
	if (
		!FMath::IsFinite(ModifierPolicy.MaxTargetFollowSpeedCmPerSecond) ||
		!FMath::IsFinite(ModifierPolicy.MaxTargetAngularSpeedDegreesPerSecond) ||
		!FMath::IsFinite(ModifierPolicy.TargetInterpolationSpeed) ||
		!FMath::IsFinite(ModifierPolicy.TargetTeleportThresholdCm) ||
		!FMath::IsFinite(ModifierPolicy.TargetLostFadeSeconds) ||
		ModifierPolicy.MaxTargetFollowSpeedCmPerSecond <= 0.0f ||
		ModifierPolicy.MaxTargetAngularSpeedDegreesPerSecond <= 0.0f ||
		ModifierPolicy.TargetInterpolationSpeed <= 0.0f ||
		ModifierPolicy.TargetTeleportThresholdCm <= 0.0f ||
		ModifierPolicy.TargetLostFadeSeconds <= 0.0f ||
		!FMath::IsFinite(ModifierPolicy.MinObstacleAmplitudeScale) ||
		!FMath::IsFinite(ModifierPolicy.MinObstacleReachScale) ||
		!FMath::IsFinite(ModifierPolicy.ObstacleCancelClearance) ||
		ModifierPolicy.MinObstacleAmplitudeScale <= 0.0f ||
		ModifierPolicy.MinObstacleAmplitudeScale > 1.0f ||
		ModifierPolicy.MinObstacleReachScale <= 0.0f ||
		ModifierPolicy.MinObstacleReachScale > 1.0f ||
		ModifierPolicy.ObstacleCancelClearance < 0.0f ||
		ModifierPolicy.ObstacleCancelClearance > 1.0f
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_CONTEXT_POLICY_INVALID");
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

	if (IsPublished())
	{
		const FString VisualDescription =
			Metadata.VisualDescription.TrimStartAndEnd();
		if (
			Metadata.CatalogSchemaVersion != LLMNPCCatalog::SchemaVersion ||
			Metadata.CatalogRevision < 1
		)
		{
			OutError = TEXT("LLMNPC_TEMPLATE_CATALOG_VERSION_INVALID");
			return false;
		}
		if (VisualDescription.IsEmpty() || VisualDescription.Len() > 600)
		{
			OutError = TEXT("LLMNPC_TEMPLATE_VISUAL_DESCRIPTION_INVALID");
			return false;
		}
		if (
			Metadata.BodyRegionTags.IsEmpty() ||
			Metadata.RequiredCapabilities.IsEmpty() ||
			(Kind != ELLMNPCTemplateKind::AnimationAsset && Metadata.RequiredChannels.IsEmpty()) ||
			(Metadata.IntentTags.IsEmpty() && Metadata.SemanticEffectTags.IsEmpty()) ||
			!HasUniqueNonEmptyNames(Metadata.BodyRegionTags) ||
			!HasUniqueNonEmptyNames(Metadata.SpatialRequirementTags) ||
			!HasUniqueNonEmptyNames(Metadata.SemanticEffectTags) ||
			!HasUniqueNonEmptyNames(Metadata.TargetCategoryTags) ||
			!HasUniqueNonEmptyNames(Metadata.RequiredCapabilities)
		)
		{
			OutError = TEXT("LLMNPC_TEMPLATE_CATALOG_METADATA_INVALID");
			return false;
		}
		if (Metadata.bRequiresTarget != !Metadata.TargetCategoryTags.IsEmpty())
		{
			OutError = TEXT("LLMNPC_TEMPLATE_TARGET_CONTRACT_INVALID");
			return false;
		}
		if (
			!FMath::IsFinite(Metadata.Expressiveness) ||
			!FMath::IsFinite(Metadata.Energy) ||
			!FMath::IsFinite(Metadata.SocialIntensity) ||
			Metadata.Expressiveness < 0.0f ||
			Metadata.Expressiveness > 1.0f ||
			Metadata.Energy < 0.0f ||
			Metadata.Energy > 1.0f ||
			Metadata.SocialIntensity < 0.0f ||
			Metadata.SocialIntensity > 1.0f
		)
		{
			OutError = TEXT("LLMNPC_TEMPLATE_CATALOG_SCALES_INVALID");
			return false;
		}
		if (
			Metadata.CatalogContentHash.IsEmpty() ||
			Metadata.CatalogContentHash != BuildCatalogContentHash(*this)
		)
		{
			OutError = TEXT("LLMNPC_TEMPLATE_CATALOG_CONTENT_HASH_STALE");
			return false;
		}
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

FString ULLMNPCMotionTemplate::BuildCatalogContentHash(
	const ULLMNPCMotionTemplate& Template
)
{
	const FLLMNPCTemplateMetadata& Metadata = Template.Metadata;
	const FLLMNPCModifierPolicy& Policy = Template.ModifierPolicy;
	TArray<FString> Lines = {
		Metadata.TemplateId.ToString().ToLower(),
		Metadata.PublicActionId.ToString().ToLower(),
		Metadata.SemanticVersion.TrimStartAndEnd(),
		FString::FromInt(Metadata.CatalogRevision),
		Metadata.VariantId.ToString().ToLower(),
		JoinNames(Metadata.VariantStyleTags),
		FString::Printf(TEXT("%.6f"), Metadata.VariantWeight),
		Metadata.DisplayName.ToString().TrimStartAndEnd(),
		Metadata.VisualDescription.TrimStartAndEnd(),
		JoinNames(Metadata.IntentTags),
		JoinNames(Metadata.EmotionTags),
		JoinNames(Metadata.PersonalityTags),
		JoinNames(Metadata.BodyRegionTags),
		JoinNames(Metadata.SpatialRequirementTags),
		JoinNames(Metadata.SemanticEffectTags),
		JoinNames(Metadata.TargetCategoryTags),
		JoinNames(Metadata.RequiredCapabilities),
		JoinNames(Metadata.RequiredChannels),
		JoinNames(Metadata.BlockedStates),
		Metadata.SkeletonProfileId.ToString().ToLower(),
		JoinNames(Metadata.CompatibleSkeletonProfileIds),
		Metadata.bRequiresTarget ? TEXT("1") : TEXT("0"),
		Metadata.bCanRunWhileMoving ? TEXT("1") : TEXT("0"),
		Metadata.bAllowRuntimeModelSelection ? TEXT("1") : TEXT("0"),
		FString::Printf(TEXT("%.6f"), Metadata.CooldownSeconds),
		FString::Printf(TEXT("%.6f"), Metadata.Expressiveness),
		FString::Printf(TEXT("%.6f"), Metadata.Energy),
		FString::Printf(TEXT("%.6f"), Metadata.SocialIntensity),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.AmplitudeRange.X, Policy.AmplitudeRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.SpeedRange.X, Policy.SpeedRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.DurationRange.X, Policy.DurationRange.Y),
		Policy.bAllowMirror ? TEXT("1") : TEXT("0"),
		JoinNames(Policy.AllowedStyleTags),
		FString::FromInt(Policy.PolicyVersion),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.ReachScaleRange.X, Policy.ReachScaleRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.HeightScaleRange.X, Policy.HeightScaleRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.LateralScaleRange.X, Policy.LateralScaleRange.Y),
		FString::Printf(TEXT("%d,%d"), Policy.CycleCountRange.X, Policy.CycleCountRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.GazeEngagementRange.X, Policy.GazeEngagementRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.PalmOrientationWeightRange.X, Policy.PalmOrientationWeightRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.FingerPoseWeightRange.X, Policy.FingerPoseWeightRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.TorsoParticipationRange.X, Policy.TorsoParticipationRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.BlendInScaleRange.X, Policy.BlendInScaleRange.Y),
		FString::Printf(TEXT("%.6f,%.6f"), Policy.BlendOutScaleRange.X, Policy.BlendOutScaleRange.Y),
		Policy.bEnableDynamicTargetTracking ? TEXT("1") : TEXT("0"),
		FString::Printf(TEXT("%.6f"), Policy.MaxTargetFollowSpeedCmPerSecond),
		FString::Printf(TEXT("%.6f"), Policy.MaxTargetAngularSpeedDegreesPerSecond),
		FString::Printf(TEXT("%.6f"), Policy.TargetInterpolationSpeed),
		FString::Printf(TEXT("%.6f"), Policy.TargetTeleportThresholdCm),
		FString::Printf(TEXT("%.6f"), Policy.TargetLostFadeSeconds),
		FString::FromInt(static_cast<int32>(Policy.TargetLossPolicy)),
		Policy.bEnableObstacleAdaptation ? TEXT("1") : TEXT("0"),
		FString::Printf(TEXT("%.6f"), Policy.MinObstacleAmplitudeScale),
		FString::Printf(TEXT("%.6f"), Policy.MinObstacleReachScale),
		FString::Printf(TEXT("%.6f"), Policy.ObstacleCancelClearance),
		Metadata.VariantDifference.TrimStartAndEnd(),
		Metadata.SourceRecipeHash.TrimStartAndEnd(),
		Metadata.KinematicReportHash.TrimStartAndEnd(),
		Metadata.CatalogSchemaVersion
	};
	if (Template.Kind == ELLMNPCTemplateKind::AnimationAsset)
	{
		const FLLMNPCAnimationPlaybackPolicy& Playback =
			Template.AnimationPlayback;
		Lines.Append({
			TEXT("animation_asset_v1"),
			Template.AnimationAsset.ToSoftObjectPath().ToString(),
			Playback.SlotName.ToString().ToLower(),
			FString::Printf(TEXT("%.6f"), Playback.BlendInSeconds),
			FString::Printf(TEXT("%.6f"), Playback.BlendOutSeconds),
			FString::Printf(TEXT("%.6f"), Playback.StartPositionSeconds),
			FString::Printf(TEXT("%.6f"), Playback.MaxDurationSeconds),
			Playback.bLoop ? TEXT("1") : TEXT("0"),
			Playback.bInterruptible ? TEXT("1") : TEXT("0"),
			Playback.bStopOtherMontages ? TEXT("1") : TEXT("0"),
			Playback.bAllowRootMotion ? TEXT("1") : TEXT("0")
		});
	}
	return FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*FString::Join(Lines, TEXT("\n")))
	);
}

FPrimaryAssetId ULLMNPCMotionTemplate::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("LLMNPCTemplate"), GetFName());
}
