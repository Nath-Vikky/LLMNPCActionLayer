#include "LLMNPCMotionValidator.h"

FLLMMotionValidationResult ULLMNPCMotionValidator::ValidateAndClamp(FLLMMotionPlan& InOutPlan) const
{
	FLLMMotionValidationResult Result;

	FLLMMotionClip& Clip = InOutPlan.Clip;
	Clip.Duration = FMath::Clamp(Clip.Duration, 0.05f, MaxClipDuration);
	Clip.BlendIn = FMath::Clamp(Clip.BlendIn, 0.0f, Clip.Duration);
	Clip.BlendOut = FMath::Clamp(Clip.BlendOut, 0.0f, Clip.Duration);
	Clip.Priority = FMath::Clamp(Clip.Priority, 0.0f, 1.0f);

	if (Clip.Tracks.Num() > MaxTracks)
	{
		Clip.Tracks.SetNum(MaxTracks);
	}

	for (FLLMMotionTrack& Track : Clip.Tracks)
	{
		FString Error;
		if (!ValidateTrack(Track, Error))
		{
			Result.bValid = false;
			Result.ErrorMessage = Error;
			return Result;
		}
	}

	Result.bValid = true;
	return Result;
}

bool ULLMNPCMotionValidator::ValidateTrack(FLLMMotionTrack& InOutTrack, FString& OutError) const
{
	const FLLMControlDefinition* Def = FindControl(InOutTrack.ControlId);
	if (!Def)
	{
		OutError = FString::Printf(TEXT("Unknown ControlId: %s"), *InOutTrack.ControlId.ToString());
		return false;
	}

	if (!Def->bAllowLLM)
	{
		OutError = FString::Printf(TEXT("Control is not exposed to LLM: %s"), *InOutTrack.ControlId.ToString());
		return false;
	}

	if (!Def->AllowedTrackTypes.Contains(InOutTrack.TrackType))
	{
		OutError = FString::Printf(TEXT("Track type is not allowed for ControlId: %s"), *InOutTrack.ControlId.ToString());
		return false;
	}

	if (InOutTrack.EndTime < InOutTrack.StartTime)
	{
		Swap(InOutTrack.StartTime, InOutTrack.EndTime);
	}

	InOutTrack.StartTime = FMath::Max(0.0f, InOutTrack.StartTime);
	InOutTrack.EndTime = FMath::Max(InOutTrack.StartTime, InOutTrack.EndTime);
	InOutTrack.Amplitude = FMath::Clamp(InOutTrack.Amplitude, Def->MinValue, Def->MaxValue);
	InOutTrack.Frequency = FMath::Clamp(InOutTrack.Frequency, 0.0f, 8.0f);
	InOutTrack.Strength = FMath::Clamp(InOutTrack.Strength, 0.0f, 1.0f);
	InOutTrack.Reach = FMath::Clamp(InOutTrack.Reach, 0.0f, 1.0f);
	InOutTrack.Offset.X = FMath::Clamp(InOutTrack.Offset.X, -50.0f, 50.0f);
	InOutTrack.Offset.Y = FMath::Clamp(InOutTrack.Offset.Y, -50.0f, 50.0f);
	InOutTrack.Offset.Z = FMath::Clamp(InOutTrack.Offset.Z, -50.0f, 50.0f);

	if (InOutTrack.FloatKeys.Num() > MaxFloatKeysPerTrack)
	{
		InOutTrack.FloatKeys.SetNum(MaxFloatKeysPerTrack);
	}

	for (FLLMMotionKeyFloat& Key : InOutTrack.FloatKeys)
	{
		Key.T = FMath::Clamp(Key.T, InOutTrack.StartTime, InOutTrack.EndTime);
		Key.V = FMath::Clamp(Key.V, Def->MinValue, Def->MaxValue);
	}

	if (Def->bRequiresTarget && InOutTrack.TargetRef.TrimStartAndEnd().IsEmpty())
	{
		OutError = FString::Printf(TEXT("ControlId requires TargetRef: %s"), *InOutTrack.ControlId.ToString());
		return false;
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
