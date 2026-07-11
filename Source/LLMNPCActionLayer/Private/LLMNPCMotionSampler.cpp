#include "LLMNPCMotionSampler.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

namespace
{
float Smooth01(float Value)
{
	const float T = FMath::Clamp(Value, 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

float EvaluateClipAlpha(const FLLMMotionClip& Clip, float Time)
{
	const float InAlpha = Clip.BlendIn > KINDA_SMALL_NUMBER
		? FMath::Clamp(Time / Clip.BlendIn, 0.0f, 1.0f)
		: 1.0f;

	const float OutAlpha = Clip.BlendOut > KINDA_SMALL_NUMBER
		? FMath::Clamp((Clip.Duration - Time) / Clip.BlendOut, 0.0f, 1.0f)
		: 1.0f;

	return Smooth01(FMath::Min(InAlpha, OutAlpha));
}

FVector GetBoneLocationCS(USkeletalMeshComponent* Mesh, FName BoneName, const FVector& Fallback)
{
	if (!Mesh || BoneName.IsNone())
	{
		return Fallback;
	}

	if (Mesh->DoesSocketExist(BoneName) || Mesh->GetBoneIndex(BoneName) != INDEX_NONE)
	{
		return Mesh->GetSocketTransform(BoneName, RTS_Component).GetLocation();
	}

	return Fallback;
}
}

void FLLMNPCMotionSampler::SampleClip(
	const FLLMMotionClip& Clip,
	const ULLMNPCControlManifest* Manifest,
	USkeletalMeshComponent* Mesh,
	const TMap<FString, TObjectPtr<AActor>>& TargetMap,
	float Time,
	FLLMProceduralPoseSnapshot& OutSnapshot
)
{
	OutSnapshot = FLLMProceduralPoseSnapshot();
	OutSnapshot.GlobalAlpha = EvaluateClipAlpha(Clip, Time);

	if (OutSnapshot.GlobalAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FVector PendingRightHandLocalOffsetCS = FVector::ZeroVector;
	FVector PendingLeftHandLocalOffsetCS = FVector::ZeroVector;

	for (const FLLMMotionTrack& Track : Clip.Tracks)
	{
		const float TrackAlpha = EvaluateEnvelope(Track, Time) * OutSnapshot.GlobalAlpha * Track.Strength;
		if (TrackAlpha <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FName Control = Track.ControlId;
		const float FloatValue = EvaluateFloatTrack(Track, Time) * TrackAlpha;

		if (Control == TEXT("head.pitch"))
		{
			OutSnapshot.HeadPitch += FloatValue;
		}
		else if (Control == TEXT("head.yaw"))
		{
			OutSnapshot.HeadYaw += FloatValue;
		}
		else if (Control == TEXT("head.roll"))
		{
			OutSnapshot.HeadRoll += FloatValue;
		}
		else if (Control == TEXT("chest.pitch"))
		{
			OutSnapshot.ChestPitch += FloatValue;
		}
		else if (Control == TEXT("chest.yaw"))
		{
			OutSnapshot.ChestYaw += FloatValue;
		}
		else if (Control == TEXT("chest.roll"))
		{
			OutSnapshot.ChestRoll += FloatValue;
		}
		else if (Control == TEXT("right_hand.local_offset.x"))
		{
			PendingRightHandLocalOffsetCS.X += FloatValue;
		}
		else if (Control == TEXT("right_hand.local_offset.y"))
		{
			PendingRightHandLocalOffsetCS.Y += FloatValue;
		}
		else if (Control == TEXT("right_hand.local_offset.z"))
		{
			PendingRightHandLocalOffsetCS.Z += FloatValue;
		}
		else if (Control == TEXT("left_hand.local_offset.x"))
		{
			PendingLeftHandLocalOffsetCS.X += FloatValue;
		}
		else if (Control == TEXT("left_hand.local_offset.y"))
		{
			PendingLeftHandLocalOffsetCS.Y += FloatValue;
		}
		else if (Control == TEXT("left_hand.local_offset.z"))
		{
			PendingLeftHandLocalOffsetCS.Z += FloatValue;
		}
		else if (Control == TEXT("right_upperarm.pitch"))
		{
			OutSnapshot.RightUpperArmAdditiveRotation.Pitch += FloatValue;
		}
		else if (Control == TEXT("right_upperarm.yaw"))
		{
			OutSnapshot.RightUpperArmAdditiveRotation.Yaw += FloatValue;
		}
		else if (Control == TEXT("right_upperarm.roll"))
		{
			OutSnapshot.RightUpperArmAdditiveRotation.Roll += FloatValue;
		}
		else if (Control == TEXT("right_lowerarm.pitch"))
		{
			OutSnapshot.RightLowerArmAdditiveRotation.Pitch += FloatValue;
		}
		else if (Control == TEXT("right_lowerarm.yaw"))
		{
			OutSnapshot.RightLowerArmAdditiveRotation.Yaw += FloatValue;
		}
		else if (Control == TEXT("right_lowerarm.roll"))
		{
			OutSnapshot.RightLowerArmAdditiveRotation.Roll += FloatValue;
		}
		else if (Control == TEXT("right_hand.pitch"))
		{
			OutSnapshot.RightHandAdditiveRotation.Pitch += FloatValue;
		}
		else if (Control == TEXT("right_hand.yaw"))
		{
			OutSnapshot.RightHandAdditiveRotation.Yaw += FloatValue;
		}
		else if (Control == TEXT("right_hand.roll"))
		{
			OutSnapshot.RightHandAdditiveRotation.Roll += FloatValue;
		}
		else if (Control == TEXT("right_fingers.open"))
		{
			OutSnapshot.RightFingersOpen = FMath::Max(OutSnapshot.RightFingersOpen, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("right_fingers.point"))
		{
			OutSnapshot.RightFingersPoint = FMath::Max(OutSnapshot.RightFingersPoint, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("left_fingers.open"))
		{
			OutSnapshot.LeftFingersOpen = FMath::Max(OutSnapshot.LeftFingersOpen, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("left_fingers.point"))
		{
			OutSnapshot.LeftFingersPoint = FMath::Max(OutSnapshot.LeftFingersPoint, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("right_hand.ik"))
		{
			OutSnapshot.RightHandIKAlpha = FMath::Max(OutSnapshot.RightHandIKAlpha, TrackAlpha);
			if (Track.TrackType == ELLMMotionTrackType::Anchor)
			{
				OutSnapshot.RightHandIKTargetCS = BuildAnchorTargetCS(Track, Manifest, Mesh);
			}
			else if (Track.TrackType == ELLMMotionTrackType::IKReach)
			{
				OutSnapshot.RightHandIKTargetCS = BuildReachTargetCS(Track, Mesh, TargetMap, false);
			}
		}
		else if (Control == TEXT("left_hand.ik"))
		{
			OutSnapshot.LeftHandIKAlpha = FMath::Max(OutSnapshot.LeftHandIKAlpha, TrackAlpha);
			if (Track.TrackType == ELLMMotionTrackType::Anchor)
			{
				OutSnapshot.LeftHandIKTargetCS = BuildAnchorTargetCS(Track, Manifest, Mesh);
			}
			else if (Track.TrackType == ELLMMotionTrackType::IKReach)
			{
				OutSnapshot.LeftHandIKTargetCS = BuildReachTargetCS(Track, Mesh, TargetMap, true);
			}
		}
		else if (Control == TEXT("gaze.target"))
		{
			if (const TObjectPtr<AActor>* Target = TargetMap.Find(Track.TargetRef))
			{
				if (Mesh && IsValid(Target->Get()))
				{
					OutSnapshot.GazeTargetCS = Mesh->GetComponentTransform().InverseTransformPosition(
						Target->Get()->GetActorLocation() + FVector(0.0f, 0.0f, 80.0f)
					);
					OutSnapshot.GazeAlpha = FMath::Max(OutSnapshot.GazeAlpha, TrackAlpha);
				}
			}
		}
		else if (Control == TEXT("right_hand.palm_target"))
		{
			if (const TObjectPtr<AActor>* Target = TargetMap.Find(Track.TargetRef))
			{
				if (Mesh && IsValid(Target->Get()))
				{
					OutSnapshot.RightHandPalmTargetCS = Mesh->GetComponentTransform().InverseTransformPosition(
						Target->Get()->GetActorLocation() + FVector(0.0f, 0.0f, 70.0f)
					);
					OutSnapshot.RightHandPalmAlpha = FMath::Max(OutSnapshot.RightHandPalmAlpha, TrackAlpha);
				}
			}
		}
		else if (Control == TEXT("left_hand.palm_target"))
		{
			if (const TObjectPtr<AActor>* Target = TargetMap.Find(Track.TargetRef))
			{
				if (Mesh && IsValid(Target->Get()))
				{
					OutSnapshot.LeftHandPalmTargetCS = Mesh->GetComponentTransform().InverseTransformPosition(
						Target->Get()->GetActorLocation() + FVector(0.0f, 0.0f, 70.0f)
					);
					OutSnapshot.LeftHandPalmAlpha = FMath::Max(OutSnapshot.LeftHandPalmAlpha, TrackAlpha);
				}
			}
		}
	}

	OutSnapshot.RightHandLocalOffsetCS = PendingRightHandLocalOffsetCS;
	if (OutSnapshot.RightHandIKAlpha > KINDA_SMALL_NUMBER)
	{
		OutSnapshot.RightHandIKTargetCS += PendingRightHandLocalOffsetCS;
	}
	OutSnapshot.LeftHandLocalOffsetCS = PendingLeftHandLocalOffsetCS;
	if (OutSnapshot.LeftHandIKAlpha > KINDA_SMALL_NUMBER)
	{
		OutSnapshot.LeftHandIKTargetCS += PendingLeftHandLocalOffsetCS;
	}
}

float FLLMNPCMotionSampler::EvaluateFloatTrack(const FLLMMotionTrack& Track, float Time)
{
	switch (Track.TrackType)
	{
	case ELLMMotionTrackType::Oscillator:
		{
			const float Duration = FMath::Max(KINDA_SMALL_NUMBER, Track.EndTime - Track.StartTime);
			const float Normalized = FMath::Clamp((Time - Track.StartTime) / Duration, 0.0f, 1.0f);
			return FMath::Sin((Normalized * Track.Frequency * 2.0f * PI) + Track.Phase) * Track.Amplitude;
		}
	case ELLMMotionTrackType::Keyframes:
		return EvaluateKeyframes(Track.FloatKeys, Time);
	case ELLMMotionTrackType::Hold:
	case ELLMMotionTrackType::Spring:
		return Track.Amplitude;
	default:
		return Track.Amplitude;
	}
}

float FLLMNPCMotionSampler::EvaluateEnvelope(const FLLMMotionTrack& Track, float Time)
{
	if (Time < Track.StartTime || Time > Track.EndTime)
	{
		return 0.0f;
	}

	const float Duration = FMath::Max(KINDA_SMALL_NUMBER, Track.EndTime - Track.StartTime);
	const float Normalized = FMath::Clamp((Time - Track.StartTime) / Duration, 0.0f, 1.0f);

	switch (Track.Envelope)
	{
	case ELLMMotionEnvelope::Smooth:
	case ELLMMotionEnvelope::EaseInOut:
		return FMath::Sin(Normalized * PI);
	case ELLMMotionEnvelope::EaseIn:
		return Smooth01(Normalized);
	case ELLMMotionEnvelope::EaseOut:
		return 1.0f - Smooth01(1.0f - Normalized);
	default:
		return 1.0f;
	}
}

float FLLMNPCMotionSampler::EvaluateKeyframes(const TArray<FLLMMotionKeyFloat>& Keys, float Time)
{
	if (Keys.Num() == 0)
	{
		return 0.0f;
	}

	if (Time <= Keys[0].T)
	{
		return Keys[0].V;
	}

	for (int32 Index = 0; Index < Keys.Num() - 1; ++Index)
	{
		const FLLMMotionKeyFloat& A = Keys[Index];
		const FLLMMotionKeyFloat& B = Keys[Index + 1];
		if (Time <= B.T)
		{
			const float Alpha = FMath::Clamp((Time - A.T) / FMath::Max(KINDA_SMALL_NUMBER, B.T - A.T), 0.0f, 1.0f);
			return FMath::Lerp(A.V, B.V, Smooth01(Alpha));
		}
	}

	return Keys.Last().V;
}

FVector FLLMNPCMotionSampler::BuildAnchorTargetCS(
	const FLLMMotionTrack& Track,
	const ULLMNPCControlManifest* Manifest,
	USkeletalMeshComponent* Mesh
)
{
	const FLLMAnchorDefinition* AnchorDef = Manifest ? Manifest->FindAnchor(Track.Anchor) : nullptr;
	if (!AnchorDef)
	{
		AnchorDef = ULLMNPCControlManifest::FindBuiltInAnchor(Track.Anchor);
	}

	if (!AnchorDef)
	{
		return Track.Offset;
	}

	const FVector Base = GetBoneLocationCS(Mesh, AnchorDef->BoneName, FVector::ZeroVector);
	return Base + AnchorDef->OffsetCS + Track.Offset;
}

FVector FLLMNPCMotionSampler::BuildReachTargetCS(
	const FLLMMotionTrack& Track,
	USkeletalMeshComponent* Mesh,
	const TMap<FString, TObjectPtr<AActor>>& TargetMap,
	bool bLeftHand
)
{
	if (!Mesh)
	{
		return FVector::ZeroVector;
	}

	const TObjectPtr<AActor>* Target = TargetMap.Find(Track.TargetRef);
	if (!Target || !IsValid(Target->Get()))
	{
		return FVector::ZeroVector;
	}

	const FVector TargetCS = Mesh->GetComponentTransform().InverseTransformPosition(
		Target->Get()->GetActorLocation() + FVector(0.0f, 0.0f, 60.0f)
	);

	const FVector ShoulderCS = GetBoneLocationCS(
		Mesh,
		bLeftHand ? FName(TEXT("upperarm_l")) : FName(TEXT("upperarm_r")),
		FVector(0.0f, bLeftHand ? -25.0f : 25.0f, 80.0f)
	);
	const FVector Direction = (TargetCS - ShoulderCS).GetSafeNormal();
	const float ReachDistance = FMath::Lerp(35.0f, 90.0f, Track.Reach);
	return ShoulderCS + Direction * ReachDistance + Track.Offset;
}
