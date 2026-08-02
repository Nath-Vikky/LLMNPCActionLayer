#include "Templates/LLMNPCTemplateCompiler.h"

#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Style/LLMNPCStyleResolver.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
bool TrackAcceptsResolvedTarget(const FLLMMotionTrack& Track)
{
	return
		!Track.TargetRef.IsEmpty() ||
		Track.TrackType == ELLMMotionTrackType::IKReach ||
		Track.TrackType == ELLMMotionTrackType::LookAt ||
		Track.ControlId == TEXT("right_hand.palm_target") ||
		Track.ControlId == TEXT("left_hand.palm_target") ||
		Track.ControlId == TEXT("right_hand.palm_facing") ||
		Track.ControlId == TEXT("left_hand.palm_facing");
}

bool MirrorTrack(FLLMMotionTrack& Track)
{
	if (Track.ControlId == TEXT("gaze.target"))
	{
		return true;
	}
	static const TMap<FName, FName> ControlMap = {
		{TEXT("right_shoulder.pitch"), TEXT("left_shoulder.pitch")},
		{TEXT("right_shoulder.yaw"), TEXT("left_shoulder.yaw")},
		{TEXT("right_shoulder.roll"), TEXT("left_shoulder.roll")},
		{TEXT("right_upperarm.pitch"), TEXT("mirror_left_upperarm.pitch")},
		{TEXT("right_upperarm.yaw"), TEXT("mirror_left_upperarm.yaw")},
		{TEXT("right_upperarm.roll"), TEXT("mirror_left_upperarm.roll")},
		{TEXT("right_lowerarm.pitch"), TEXT("mirror_left_lowerarm.pitch")},
		{TEXT("right_lowerarm.yaw"), TEXT("mirror_left_lowerarm.yaw")},
		{TEXT("right_lowerarm.roll"), TEXT("mirror_left_lowerarm.roll")},
		{TEXT("right_hand.pitch"), TEXT("mirror_left_hand.pitch")},
		{TEXT("right_hand.yaw"), TEXT("mirror_left_hand.yaw")},
		{TEXT("right_hand.roll"), TEXT("mirror_left_hand.roll")},
		{TEXT("right_hand.ik"), TEXT("left_hand.ik")},
		{TEXT("right_hand.local_offset.x"), TEXT("left_hand.local_offset.x")},
		{TEXT("right_hand.local_offset.y"), TEXT("left_hand.local_offset.y")},
		{TEXT("right_hand.local_offset.z"), TEXT("left_hand.local_offset.z")},
		{TEXT("right_hand.palm_target"), TEXT("left_hand.palm_target")},
		{TEXT("right_hand.palm_facing"), TEXT("left_hand.palm_facing")},
		{TEXT("right_hand.palm_up"), TEXT("left_hand.palm_up")},
		{TEXT("right_fingers.open"), TEXT("left_fingers.open")},
		{TEXT("right_fingers.point"), TEXT("left_fingers.point")},
		{TEXT("right_fingers.contact"), TEXT("left_fingers.contact")},
		{TEXT("right_fingers.relaxed"), TEXT("left_fingers.relaxed")},
		{TEXT("right_fingers.curl"), TEXT("left_fingers.curl")},
		{TEXT("right_fingers.thumbs_up"), TEXT("left_fingers.thumbs_up")}
	};
	const FName* MirroredControl = ControlMap.Find(Track.ControlId);
	if (!MirroredControl)
	{
		return false;
	}
	const bool bMirrorScalarValue =
		Track.ControlId == TEXT("right_hand.local_offset.y");
	Track.ControlId = *MirroredControl;
	Track.Offset.Y *= -1.0f;
	if (Track.Anchor == TEXT("head_right"))
	{
		Track.Anchor = TEXT("head_left");
	}
	else if (Track.Anchor == TEXT("right_wave"))
	{
		Track.Anchor = TEXT("left_wave");
	}
	else if (Track.Anchor == TEXT("right_thumbs_up"))
	{
		Track.Anchor = TEXT("left_thumbs_up");
	}
	if (bMirrorScalarValue)
	{
		Track.Amplitude *= -1.0f;
		for (FLLMMotionKeyFloat& Key : Track.FloatKeys)
		{
			Key.V *= -1.0f;
		}
	}
	return true;
}

bool IsNormalizedPoseControl(FName ControlId)
{
	const FString Control = ControlId.ToString();
	return
		Control.StartsWith(TEXT("right_fingers.")) ||
		Control.StartsWith(TEXT("left_fingers.")) ||
		Control == TEXT("gaze.target") ||
		Control == TEXT("right_hand.palm_target") ||
		Control == TEXT("left_hand.palm_target") ||
		Control == TEXT("right_hand.palm_facing") ||
		Control == TEXT("left_hand.palm_facing") ||
		Control == TEXT("right_hand.palm_up") ||
		Control == TEXT("left_hand.palm_up");
}

FVector2D IntersectPolicyRange(const FVector2D& TemplateRange, const FVector2D& ContextRange)
{
	if (ContextRange.X <= 0.0 || ContextRange.Y < ContextRange.X)
	{
		return TemplateRange;
	}
	const double MinValue = FMath::Max(TemplateRange.X, ContextRange.X);
	const double MaxValue = FMath::Min(TemplateRange.Y, ContextRange.Y);
	return MaxValue >= MinValue ? FVector2D(MinValue, MaxValue) : FVector2D::ZeroVector;
}
}

bool FLLMNPCTemplateCompiler::Compile(
	const ULLMNPCMotionTemplate& MotionTemplate,
	const FLLMNPCTemplateModifiers& Modifiers,
	const ULLMNPCSkeletonProfile& SkeletonProfile,
	FLLMMotionPlan& OutPlan,
	FString& OutError,
	FLLMNPCTemplateResolvedModifiers* OutResolvedModifiers
)
{
	OutPlan = FLLMMotionPlan();
	OutError.Reset();
	if (OutResolvedModifiers)
	{
		*OutResolvedModifiers = FLLMNPCTemplateResolvedModifiers();
	}

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

	if (!MotionTemplate.SupportsSkeletonProfile(SkeletonProfile.ProfileId))
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
		if (!MotionTemplate.ModifierPolicy.bAllowMirror)
		{
			OutError = TEXT("LLMNPC_TEMPLATE_MIRROR_FORBIDDEN");
			return false;
		}
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

	FRandomStream RandomStream(Modifiers.RandomSeed);
	const float AmplitudeJitter = Modifiers.RandomSeed == 0
		? 1.0f
		: 1.0f + RandomStream.FRandRange(
			-MotionTemplate.ModifierPolicy.RandomAmplitudeJitter,
			MotionTemplate.ModifierPolicy.RandomAmplitudeJitter
		);
	const float SpeedJitter = Modifiers.RandomSeed == 0
		? 1.0f
		: 1.0f + RandomStream.FRandRange(
			-MotionTemplate.ModifierPolicy.RandomSpeedJitter,
			MotionTemplate.ModifierPolicy.RandomSpeedJitter
		);
	const FLLMNPCStylePreset StylePreset = ULLMNPCStyleResolver::GetBuiltInPreset(Modifiers.Style);
	const FVector2D AmplitudeRange = IntersectPolicyRange(
		MotionTemplate.ModifierPolicy.AmplitudeRange,
		Modifiers.ContextAmplitudeRange
	);
	const FVector2D SpeedRange = IntersectPolicyRange(
		MotionTemplate.ModifierPolicy.SpeedRange,
		Modifiers.ContextSpeedRange
	);
	const FVector2D DurationRange = IntersectPolicyRange(
		MotionTemplate.ModifierPolicy.DurationRange,
		Modifiers.ContextDurationRange
	);
	if (AmplitudeRange.X <= 0.0 || SpeedRange.X <= 0.0 || DurationRange.X <= 0.0)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_CONTEXT_POLICY_INCOMPATIBLE");
		return false;
	}
	const float Amplitude = FMath::Clamp(
		Modifiers.Amplitude * AmplitudeJitter,
		AmplitudeRange.X,
		AmplitudeRange.Y
	);
	const float SpeedScale = FMath::Clamp(
		Modifiers.SpeedScale * SpeedJitter,
		SpeedRange.X,
		SpeedRange.Y
	);
	const float DurationScale = FMath::Clamp(
		Modifiers.DurationScale,
		DurationRange.X,
		DurationRange.Y
	);
	const float TimeScale = DurationScale / FMath::Max(SpeedScale, KINDA_SMALL_NUMBER);

	OutPlan.Version = TEXT("1.0");
	OutPlan.Intent = MotionTemplate.Metadata.PublicActionId.ToString();
	OutPlan.Clip = MotionTemplate.ProceduralClip;
	OutPlan.Clip.ClipId = FString::Printf(
		TEXT("%s:%s:%d%s"),
		*MotionTemplate.Metadata.TemplateId.ToString(),
		*StylePreset.StyleTag.ToString(),
		Modifiers.RandomSeed,
		Modifiers.bMirror ? TEXT(":mirror") : TEXT("")
	);
	OutPlan.Clip.Duration *= TimeScale;
	OutPlan.Clip.BlendIn *= TimeScale * StylePreset.BlendTimeScale;
	OutPlan.Clip.BlendOut *= TimeScale * StylePreset.BlendTimeScale;

	for (FLLMMotionTrack& Track : OutPlan.Clip.Tracks)
	{
		const bool bScaleTrackValue = !IsNormalizedPoseControl(Track.ControlId);
		Track.StartTime *= TimeScale;
		Track.EndTime *= TimeScale;
		if (bScaleTrackValue)
		{
			Track.Amplitude *= Amplitude;
		}
		Track.Offset *= Amplitude * StylePreset.OffsetScale;
		if (Track.TrackType == ELLMMotionTrackType::Oscillator)
		{
			const float FrequencyJitter = Modifiers.RandomSeed == 0
				? 1.0f
				: 1.0f + RandomStream.FRandRange(
					-MotionTemplate.ModifierPolicy.RandomFrequencyJitter,
					MotionTemplate.ModifierPolicy.RandomFrequencyJitter
				);
			Track.Frequency *= StylePreset.FrequencyScale * FrequencyJitter;
			const float PhaseLimit = FMath::Min(
				StylePreset.MaxPhaseJitterRadians,
				MotionTemplate.ModifierPolicy.RandomPhaseJitterRadians
			);
			if (Modifiers.RandomSeed != 0 && PhaseLimit > 0.0f)
			{
				Track.Phase += RandomStream.FRandRange(-PhaseLimit, PhaseLimit);
			}
		}

		for (FLLMMotionKeyFloat& Key : Track.FloatKeys)
		{
			Key.T *= TimeScale;
			if (bScaleTrackValue)
			{
				Key.V *= Amplitude;
			}
		}

		if (TrackAcceptsResolvedTarget(Track) && !TargetRef.IsEmpty())
		{
			Track.TargetRef = TargetRef;
		}
		if (Modifiers.bMirror && !MirrorTrack(Track))
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_TEMPLATE_MIRROR_CONTROL_UNSUPPORTED:%s"),
				*Track.ControlId.ToString()
			);
			return false;
		}
	}

	if (OutResolvedModifiers)
	{
		OutResolvedModifiers->TargetRef = TargetRef;
		OutResolvedModifiers->Amplitude = Amplitude;
		OutResolvedModifiers->SpeedScale = SpeedScale;
		OutResolvedModifiers->DurationScale = DurationScale;
		OutResolvedModifiers->Style = StylePreset.StyleTag;
		OutResolvedModifiers->bMirror = Modifiers.bMirror;
		OutResolvedModifiers->RandomSeed = Modifiers.RandomSeed;
	}
	return true;
}
