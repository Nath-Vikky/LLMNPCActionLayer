#include "Templates/LLMNPCTemplateCompiler.h"

#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
bool TrackAcceptsResolvedTarget(const FLLMMotionTrack& Track)
{
	return
		Track.TrackType == ELLMMotionTrackType::IKReach ||
		Track.TrackType == ELLMMotionTrackType::LookAt ||
		Track.ControlId == TEXT("right_hand.palm_target");
}
}

bool FLLMNPCTemplateCompiler::Compile(
	const ULLMNPCMotionTemplate& MotionTemplate,
	const FLLMNPCTemplateModifiers& Modifiers,
	const ULLMNPCSkeletonProfile& SkeletonProfile,
	FLLMMotionPlan& OutPlan,
	FString& OutError
)
{
	OutPlan = FLLMMotionPlan();
	OutError.Reset();

	FString TemplateError;
	if (!MotionTemplate.ValidateTemplate(TemplateError))
	{
		OutError = TemplateError;
		return false;
	}

	if (!MotionTemplate.IsPublished())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_NOT_PUBLISHED");
		return false;
	}

	if (MotionTemplate.Kind != ELLMNPCTemplateKind::ProceduralMotion)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_KIND_NOT_PROCEDURAL");
		return false;
	}

	if (MotionTemplate.Metadata.SkeletonProfileId != SkeletonProfile.ProfileId)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_SKELETON_PROFILE_MISMATCH");
		return false;
	}

	if (
		!FMath::IsFinite(Modifiers.Amplitude) ||
		!FMath::IsFinite(Modifiers.SpeedScale) ||
		!FMath::IsFinite(Modifiers.DurationScale)
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_MODIFIER_NON_FINITE");
		return false;
	}

	if (Modifiers.bMirror)
	{
		OutError = MotionTemplate.ModifierPolicy.bAllowMirror
			? TEXT("LLMNPC_TEMPLATE_MIRROR_NOT_IMPLEMENTED")
			: TEXT("LLMNPC_TEMPLATE_MIRROR_FORBIDDEN");
		return false;
	}

	if (
		!Modifiers.Style.IsNone() &&
		!MotionTemplate.ModifierPolicy.AllowedStyleTags.IsEmpty() &&
		!MotionTemplate.ModifierPolicy.AllowedStyleTags.Contains(Modifiers.Style)
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_STYLE_FORBIDDEN");
		return false;
	}

	const FString TargetRef = Modifiers.TargetRef.TrimStartAndEnd();
	if (MotionTemplate.Metadata.bRequiresTarget && TargetRef.IsEmpty())
	{
		OutError = TEXT("LLMNPC_TEMPLATE_TARGET_REQUIRED");
		return false;
	}

	const float Amplitude = FMath::Clamp(
		Modifiers.Amplitude,
		MotionTemplate.ModifierPolicy.AmplitudeRange.X,
		MotionTemplate.ModifierPolicy.AmplitudeRange.Y
	);
	const float SpeedScale = FMath::Clamp(
		Modifiers.SpeedScale,
		MotionTemplate.ModifierPolicy.SpeedRange.X,
		MotionTemplate.ModifierPolicy.SpeedRange.Y
	);
	const float DurationScale = FMath::Clamp(
		Modifiers.DurationScale,
		MotionTemplate.ModifierPolicy.DurationRange.X,
		MotionTemplate.ModifierPolicy.DurationRange.Y
	);
	const float TimeScale = DurationScale / FMath::Max(SpeedScale, KINDA_SMALL_NUMBER);

	OutPlan.Version = TEXT("1.0");
	OutPlan.Intent = MotionTemplate.Metadata.PublicActionId.ToString();
	OutPlan.Clip = MotionTemplate.ProceduralClip;
	OutPlan.Clip.ClipId = MotionTemplate.Metadata.TemplateId.ToString();
	OutPlan.Clip.Duration *= TimeScale;
	OutPlan.Clip.BlendIn *= TimeScale;
	OutPlan.Clip.BlendOut *= TimeScale;

	for (FLLMMotionTrack& Track : OutPlan.Clip.Tracks)
	{
		Track.StartTime *= TimeScale;
		Track.EndTime *= TimeScale;
		Track.Amplitude *= Amplitude;
		Track.Offset *= Amplitude;

		for (FLLMMotionKeyFloat& Key : Track.FloatKeys)
		{
			Key.T *= TimeScale;
			Key.V *= Amplitude;
		}

		if (TrackAcceptsResolvedTarget(Track) && !TargetRef.IsEmpty())
		{
			Track.TargetRef = TargetRef;
		}
	}

	return true;
}
