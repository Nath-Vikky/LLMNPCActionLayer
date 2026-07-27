#include "LLMNPCMotionValidator.h"

#include "Protocol/LLMNPCProtocolCompatibility.h"

namespace
{
bool IsFiniteVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
}

bool TrackTypeRequiresTarget(ELLMMotionTrackType TrackType)
{
	return TrackType == ELLMMotionTrackType::IKReach || TrackType == ELLMMotionTrackType::LookAt;
}
}

FLLMMotionValidationResult ULLMNPCMotionValidator::ValidateAndClamp(
	FLLMMotionPlan& InOutPlan,
	ELLMNPCMotionValidationSource Source
) const
{
	FLLMMotionValidationResult Result;
	if (!FLLMNPCProtocolCompatibility::NormalizeMotionPlanVersion(
		InOutPlan.Version,
		Result.ErrorMessage
	))
	{
		return Result;
	}
	FLLMMotionClip& Clip = InOutPlan.Clip;

	if (
		!FMath::IsFinite(Clip.Duration) ||
		!FMath::IsFinite(Clip.BlendIn) ||
		!FMath::IsFinite(Clip.BlendOut) ||
		!FMath::IsFinite(Clip.Priority)
	)
	{
		Result.ErrorMessage = TEXT("LLMNPC_MOTION_CLIP_NON_FINITE");
		return Result;
	}

	if (Clip.ClipId.TrimStartAndEnd().IsEmpty())
	{
		Result.ErrorMessage = TEXT("LLMNPC_MOTION_CLIP_ID_MISSING");
		return Result;
	}

	if (Clip.Tracks.IsEmpty())
	{
		Result.ErrorMessage = TEXT("LLMNPC_MOTION_CLIP_TRACKS_EMPTY");
		return Result;
	}

	if (Clip.Tracks.Num() > MaxTracks)
	{
		Result.ErrorMessage = TEXT("LLMNPC_MOTION_TRACK_LIMIT_EXCEEDED");
		return Result;
	}

	Clip.Duration = FMath::Clamp(Clip.Duration, 0.05f, MaxClipDuration);
	Clip.BlendIn = FMath::Clamp(Clip.BlendIn, 0.0f, Clip.Duration);
	Clip.BlendOut = FMath::Clamp(Clip.BlendOut, 0.0f, Clip.Duration);
	Clip.Priority = FMath::Clamp(Clip.Priority, 0.0f, 1.0f);

	for (FLLMMotionTrack& Track : Clip.Tracks)
	{
		FString Error;
		if (!ValidateTrack(Track, Clip.Duration, Source, Error))
		{
			Result.ErrorMessage = Error;
			return Result;
		}
	}

	Result.bValid = true;
	return Result;
}

bool ULLMNPCMotionValidator::ValidateTrack(
	FLLMMotionTrack& InOutTrack,
	float ClipDuration,
	ELLMNPCMotionValidationSource Source,
	FString& OutError
) const
{
	const FLLMControlDefinition* Def = FindControl(InOutTrack.ControlId);
	if (!Def)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_CONTROL_UNKNOWN:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	if (
		Source == ELLMNPCMotionValidationSource::RuntimeModel &&
		(!Def->bAllowRuntimeModel || !Def->bAllowLLM)
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_CONTROL_INTERNAL_ONLY:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	if (
		(
			Source == ELLMNPCMotionValidationSource::PublishedTemplate ||
			Source == ELLMNPCMotionValidationSource::AuthoringSandbox ||
			Source == ELLMNPCMotionValidationSource::ReplicatedAuthority
		) &&
		!Def->bAllowTemplateAuthoring
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_CONTROL_TEMPLATE_FORBIDDEN:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	if (!Def->AllowedTrackTypes.Contains(InOutTrack.TrackType))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_TRACK_TYPE_FORBIDDEN:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	const ELLMMotionValueType ExpectedValueType =
		Def->SolverType == ELLMControlSolverType::TwoBoneIK ||
		Def->SolverType == ELLMControlSolverType::LookAt
			? ELLMMotionValueType::Vector
			: ELLMMotionValueType::Float;
	if (InOutTrack.ValueType != ExpectedValueType)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_VALUE_TYPE_MISMATCH:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	if (
		!FMath::IsFinite(InOutTrack.StartTime) ||
		!FMath::IsFinite(InOutTrack.EndTime) ||
		!FMath::IsFinite(InOutTrack.Amplitude) ||
		!FMath::IsFinite(InOutTrack.Frequency) ||
		!FMath::IsFinite(InOutTrack.Phase) ||
		!FMath::IsFinite(InOutTrack.Strength) ||
		!FMath::IsFinite(InOutTrack.Reach) ||
		!IsFiniteVector(InOutTrack.Offset)
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_TRACK_NON_FINITE:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	if (InOutTrack.EndTime < InOutTrack.StartTime)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_TRACK_TIME_REVERSED:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	InOutTrack.StartTime = FMath::Clamp(InOutTrack.StartTime, 0.0f, ClipDuration);
	InOutTrack.EndTime = FMath::Clamp(InOutTrack.EndTime, InOutTrack.StartTime, ClipDuration);
	if (InOutTrack.EndTime - InOutTrack.StartTime <= KINDA_SMALL_NUMBER)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_TRACK_DURATION_ZERO:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	InOutTrack.Amplitude = FMath::Clamp(InOutTrack.Amplitude, Def->MinValue, Def->MaxValue);
	InOutTrack.Frequency = FMath::Clamp(InOutTrack.Frequency, 0.0f, 8.0f);
	InOutTrack.Strength = FMath::Clamp(InOutTrack.Strength, 0.0f, 1.0f);
	InOutTrack.Reach = FMath::Clamp(InOutTrack.Reach, 0.0f, 1.0f);
	InOutTrack.Offset.X = FMath::Clamp(InOutTrack.Offset.X, -50.0f, 50.0f);
	InOutTrack.Offset.Y = FMath::Clamp(InOutTrack.Offset.Y, -50.0f, 50.0f);
	InOutTrack.Offset.Z = FMath::Clamp(InOutTrack.Offset.Z, -50.0f, 50.0f);
	InOutTrack.TargetRef = InOutTrack.TargetRef.TrimStartAndEnd();

	if (InOutTrack.FloatKeys.Num() > MaxFloatKeysPerTrack)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_KEY_LIMIT_EXCEEDED:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	if (InOutTrack.TrackType == ELLMMotionTrackType::Keyframes && InOutTrack.FloatKeys.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_KEYS_MISSING:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	for (FLLMMotionKeyFloat& Key : InOutTrack.FloatKeys)
	{
		if (!FMath::IsFinite(Key.T) || !FMath::IsFinite(Key.V))
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_MOTION_KEY_NON_FINITE:%s"),
				*InOutTrack.ControlId.ToString()
			);
			return false;
		}

		Key.T = FMath::Clamp(Key.T, InOutTrack.StartTime, InOutTrack.EndTime);
		Key.V = FMath::Clamp(Key.V, Def->MinValue, Def->MaxValue);
	}

	InOutTrack.FloatKeys.Sort(
		[](const FLLMMotionKeyFloat& A, const FLLMMotionKeyFloat& B)
		{
			return A.T < B.T;
		}
	);

	for (int32 Index = 1; Index < InOutTrack.FloatKeys.Num(); ++Index)
	{
		if (FMath::IsNearlyEqual(InOutTrack.FloatKeys[Index - 1].T, InOutTrack.FloatKeys[Index].T))
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_MOTION_KEY_TIME_DUPLICATE:%s"),
				*InOutTrack.ControlId.ToString()
			);
			return false;
		}
	}

	const bool bRequiresTarget = Def->bRequiresTarget || TrackTypeRequiresTarget(InOutTrack.TrackType);
	if (bRequiresTarget && InOutTrack.TargetRef.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_MOTION_TARGET_REF_MISSING:%s"),
			*InOutTrack.ControlId.ToString()
		);
		return false;
	}

	if (InOutTrack.TrackType == ELLMMotionTrackType::Anchor)
	{
		if (InOutTrack.Anchor.IsNone())
		{
			OutError = TEXT("LLMNPC_MOTION_ANCHOR_MISSING");
			return false;
		}

		const FLLMAnchorDefinition* AnchorDef = Manifest
			? Manifest->FindAnchor(InOutTrack.Anchor)
			: ULLMNPCControlManifest::FindBuiltInAnchor(InOutTrack.Anchor);
		if (!AnchorDef)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_MOTION_ANCHOR_UNKNOWN:%s"),
				*InOutTrack.Anchor.ToString()
			);
			return false;
		}
	}

	return true;
}

const FLLMControlDefinition* ULLMNPCMotionValidator::FindControl(FName ControlId) const
{
	if (Manifest)
	{
		return Manifest->FindControl(ControlId);
	}

	return ULLMNPCControlManifest::FindBuiltInControl(ControlId);
}
