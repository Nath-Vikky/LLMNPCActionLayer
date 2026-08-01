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

bool IsTargetGatedPoseControl(FName ControlId)
{
	return
		ControlId == TEXT("right_hand.palm_up") ||
		ControlId == TEXT("left_hand.palm_up") ||
		ControlId == TEXT("right_fingers.open") ||
		ControlId == TEXT("right_fingers.relaxed") ||
		ControlId == TEXT("right_fingers.curl") ||
		ControlId == TEXT("left_fingers.open") ||
		ControlId == TEXT("left_fingers.relaxed") ||
		ControlId == TEXT("left_fingers.curl");
}
}

void FLLMNPCMotionSampler::SampleClip(
	const FLLMMotionClip& Clip,
	const ULLMNPCControlManifest* Manifest,
	USkeletalMeshComponent* Mesh,
	const TMap<FString, TObjectPtr<AActor>>& TargetMap,
	float Time,
	FLLMProceduralPoseSnapshot& OutSnapshot,
	const TMap<FString, FLLMNPCTargetRuntimeSample>* RuntimeTargetSamples,
	const FLLMNPCResolvedMotionModifiers* ResolvedModifiers
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
		const float TrackAlpha =
			EvaluateEnvelope(Track, Time) *
			OutSnapshot.GlobalAlpha *
			Track.Strength;
		if (TrackAlpha <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FName Control = Track.ControlId;
		if (Control.ToString().StartsWith(TEXT("mirror_left_")))
		{
			OutSnapshot.bLeftArmFKMirroredSource = true;
		}
		float FloatValue = EvaluateFloatTrack(Track, Time) * TrackAlpha;
		if (
			!Track.TargetRef.IsEmpty() &&
			IsTargetGatedPoseControl(Control)
		)
		{
			FVector IgnoredTargetLocationWS;
			float TargetAlpha = 0.0f;
			if (!ResolveTargetLocationWS(
				Track.TargetRef,
				TargetMap,
				RuntimeTargetSamples,
				IgnoredTargetLocationWS,
				TargetAlpha
			))
			{
				FloatValue = 0.0f;
			}
			else
			{
				FloatValue *= TargetAlpha;
			}
		}

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
		else if (Control == TEXT("right_shoulder.pitch"))
		{
			OutSnapshot.RightShoulderAdditiveRotation.Pitch += FloatValue;
		}
		else if (Control == TEXT("right_shoulder.yaw"))
		{
			OutSnapshot.RightShoulderAdditiveRotation.Yaw += FloatValue;
		}
		else if (Control == TEXT("right_shoulder.roll"))
		{
			OutSnapshot.RightShoulderAdditiveRotation.Roll += FloatValue;
		}
		else if (Control == TEXT("left_shoulder.pitch"))
		{
			OutSnapshot.LeftShoulderAdditiveRotation.Pitch += FloatValue;
		}
		else if (Control == TEXT("left_shoulder.yaw"))
		{
			OutSnapshot.LeftShoulderAdditiveRotation.Yaw += FloatValue;
		}
		else if (Control == TEXT("left_shoulder.roll"))
		{
			OutSnapshot.LeftShoulderAdditiveRotation.Roll += FloatValue;
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
		else if (Control == TEXT("left_upperarm.pitch") || Control == TEXT("mirror_left_upperarm.pitch"))
		{
			OutSnapshot.LeftUpperArmAdditiveRotation.Pitch += FloatValue;
		}
		else if (Control == TEXT("left_upperarm.yaw") || Control == TEXT("mirror_left_upperarm.yaw"))
		{
			OutSnapshot.LeftUpperArmAdditiveRotation.Yaw += FloatValue;
		}
		else if (Control == TEXT("left_upperarm.roll") || Control == TEXT("mirror_left_upperarm.roll"))
		{
			OutSnapshot.LeftUpperArmAdditiveRotation.Roll += FloatValue;
		}
		else if (Control == TEXT("left_lowerarm.pitch") || Control == TEXT("mirror_left_lowerarm.pitch"))
		{
			OutSnapshot.LeftLowerArmAdditiveRotation.Pitch += FloatValue;
		}
		else if (Control == TEXT("left_lowerarm.yaw") || Control == TEXT("mirror_left_lowerarm.yaw"))
		{
			OutSnapshot.LeftLowerArmAdditiveRotation.Yaw += FloatValue;
		}
		else if (Control == TEXT("left_lowerarm.roll") || Control == TEXT("mirror_left_lowerarm.roll"))
		{
			OutSnapshot.LeftLowerArmAdditiveRotation.Roll += FloatValue;
		}
		else if (Control == TEXT("left_hand.pitch") || Control == TEXT("mirror_left_hand.pitch"))
		{
			OutSnapshot.LeftHandAdditiveRotation.Pitch += FloatValue;
		}
		else if (Control == TEXT("left_hand.yaw") || Control == TEXT("mirror_left_hand.yaw"))
		{
			OutSnapshot.LeftHandAdditiveRotation.Yaw += FloatValue;
		}
		else if (Control == TEXT("left_hand.roll") || Control == TEXT("mirror_left_hand.roll"))
		{
			OutSnapshot.LeftHandAdditiveRotation.Roll += FloatValue;
		}
		else if (Control == TEXT("right_hand.palm_up"))
		{
			OutSnapshot.RightHandPalmUp = FMath::Max(
				OutSnapshot.RightHandPalmUp,
				FMath::Clamp(FloatValue, 0.0f, 1.0f)
			);
		}
		else if (Control == TEXT("left_hand.palm_up"))
		{
			OutSnapshot.LeftHandPalmUp = FMath::Max(
				OutSnapshot.LeftHandPalmUp,
				FMath::Clamp(FloatValue, 0.0f, 1.0f)
			);
		}
		else if (Control == TEXT("right_fingers.open"))
		{
			OutSnapshot.RightFingersOpen = FMath::Max(OutSnapshot.RightFingersOpen, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("right_fingers.point"))
		{
			OutSnapshot.RightFingersPoint = FMath::Max(OutSnapshot.RightFingersPoint, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("right_fingers.contact"))
		{
			OutSnapshot.RightFingersContact = FMath::Max(OutSnapshot.RightFingersContact, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("right_fingers.relaxed"))
		{
			OutSnapshot.RightFingersRelaxed = FMath::Max(OutSnapshot.RightFingersRelaxed, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("right_fingers.curl"))
		{
			OutSnapshot.RightFingersCurl = FMath::Max(OutSnapshot.RightFingersCurl, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("left_fingers.open"))
		{
			OutSnapshot.LeftFingersOpen = FMath::Max(OutSnapshot.LeftFingersOpen, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("left_fingers.point"))
		{
			OutSnapshot.LeftFingersPoint = FMath::Max(OutSnapshot.LeftFingersPoint, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("left_fingers.contact"))
		{
			OutSnapshot.LeftFingersContact = FMath::Max(OutSnapshot.LeftFingersContact, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("left_fingers.relaxed"))
		{
			OutSnapshot.LeftFingersRelaxed = FMath::Max(OutSnapshot.LeftFingersRelaxed, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("left_fingers.curl"))
		{
			OutSnapshot.LeftFingersCurl = FMath::Max(OutSnapshot.LeftFingersCurl, FMath::Clamp(FloatValue, 0.0f, 1.0f));
		}
		else if (Control == TEXT("right_hand.ik"))
		{
			if (Track.TrackType == ELLMMotionTrackType::Anchor)
			{
				OutSnapshot.RightHandIKAlpha = FMath::Max(
					OutSnapshot.RightHandIKAlpha,
					TrackAlpha
				);
				OutSnapshot.RightHandIKTargetCS = BuildAnchorTargetCS(Track, Manifest, Mesh);
			}
			else if (Track.TrackType == ELLMMotionTrackType::IKReach)
			{
				float TargetAlpha = 0.0f;
				OutSnapshot.RightHandIKTargetCS = BuildReachTargetCS(
					Track,
					Mesh,
					TargetMap,
					RuntimeTargetSamples,
					ResolvedModifiers,
					TargetAlpha,
					false
				);
				OutSnapshot.RightHandIKAlpha = FMath::Max(
					OutSnapshot.RightHandIKAlpha,
					TrackAlpha * TargetAlpha
				);
			}
		}
		else if (Control == TEXT("left_hand.ik"))
		{
			if (Track.TrackType == ELLMMotionTrackType::Anchor)
			{
				OutSnapshot.LeftHandIKAlpha = FMath::Max(
					OutSnapshot.LeftHandIKAlpha,
					TrackAlpha
				);
				OutSnapshot.LeftHandIKTargetCS = BuildAnchorTargetCS(Track, Manifest, Mesh);
			}
			else if (Track.TrackType == ELLMMotionTrackType::IKReach)
			{
				float TargetAlpha = 0.0f;
				OutSnapshot.LeftHandIKTargetCS = BuildReachTargetCS(
					Track,
					Mesh,
					TargetMap,
					RuntimeTargetSamples,
					ResolvedModifiers,
					TargetAlpha,
					true
				);
				OutSnapshot.LeftHandIKAlpha = FMath::Max(
					OutSnapshot.LeftHandIKAlpha,
					TrackAlpha * TargetAlpha
				);
			}
		}
		else if (Control == TEXT("gaze.target"))
		{
			FVector TargetLocationWS;
			float TargetAlpha = 0.0f;
			if (
				Mesh &&
				ResolveTargetLocationWS(
					Track.TargetRef,
					TargetMap,
					RuntimeTargetSamples,
					TargetLocationWS,
					TargetAlpha
				)
			)
			{
				OutSnapshot.GazeTargetCS =
					Mesh->GetComponentTransform().InverseTransformPosition(
						TargetLocationWS + FVector(0.0f, 0.0f, 80.0f)
					);
				OutSnapshot.GazeAlpha = FMath::Max(
					OutSnapshot.GazeAlpha,
					TrackAlpha * TargetAlpha
				);
			}
		}
		else if (Control == TEXT("right_hand.palm_target"))
		{
			if (Track.TrackType == ELLMMotionTrackType::Anchor)
			{
				OutSnapshot.RightHandPalmTargetCS =
					BuildAnchorTargetCS(Track, Manifest, Mesh);
				OutSnapshot.RightHandPalmAlpha = FMath::Max(
					OutSnapshot.RightHandPalmAlpha,
					TrackAlpha
				);
			}
			else
			{
				FVector TargetLocationWS;
				float TargetAlpha = 0.0f;
				if (
					Mesh &&
					ResolveTargetLocationWS(
						Track.TargetRef,
						TargetMap,
						RuntimeTargetSamples,
						TargetLocationWS,
						TargetAlpha
					)
				)
				{
					OutSnapshot.RightHandPalmTargetCS =
						Mesh->GetComponentTransform().InverseTransformPosition(
							TargetLocationWS + FVector(0.0f, 0.0f, 70.0f)
						);
					OutSnapshot.RightHandPalmAlpha = FMath::Max(
						OutSnapshot.RightHandPalmAlpha,
						TrackAlpha * TargetAlpha
					);
				}
			}
		}
		else if (Control == TEXT("left_hand.palm_target"))
		{
			if (Track.TrackType == ELLMMotionTrackType::Anchor)
			{
				OutSnapshot.LeftHandPalmTargetCS =
					BuildAnchorTargetCS(Track, Manifest, Mesh);
				OutSnapshot.LeftHandPalmAlpha = FMath::Max(
					OutSnapshot.LeftHandPalmAlpha,
					TrackAlpha
				);
			}
			else
			{
				FVector TargetLocationWS;
				float TargetAlpha = 0.0f;
				if (
					Mesh &&
					ResolveTargetLocationWS(
						Track.TargetRef,
						TargetMap,
						RuntimeTargetSamples,
						TargetLocationWS,
						TargetAlpha
					)
				)
				{
					OutSnapshot.LeftHandPalmTargetCS =
						Mesh->GetComponentTransform().InverseTransformPosition(
							TargetLocationWS + FVector(0.0f, 0.0f, 70.0f)
						);
					OutSnapshot.LeftHandPalmAlpha = FMath::Max(
						OutSnapshot.LeftHandPalmAlpha,
						TrackAlpha * TargetAlpha
					);
				}
			}
		}
		else if (Control == TEXT("right_hand.palm_facing"))
		{
			if (Track.TrackType == ELLMMotionTrackType::Anchor)
			{
				OutSnapshot.RightHandPalmFacingTargetCS =
					BuildAnchorTargetCS(Track, Manifest, Mesh);
				OutSnapshot.RightHandPalmFacingAlpha = FMath::Max(
					OutSnapshot.RightHandPalmFacingAlpha,
					TrackAlpha
				);
			}
			else
			{
				FVector TargetLocationWS;
				float TargetAlpha = 0.0f;
				if (
					Mesh &&
					ResolveTargetLocationWS(
						Track.TargetRef,
						TargetMap,
						RuntimeTargetSamples,
						TargetLocationWS,
						TargetAlpha
					)
				)
				{
					OutSnapshot.RightHandPalmFacingTargetCS =
						Mesh->GetComponentTransform().InverseTransformPosition(
							TargetLocationWS + FVector(0.0f, 0.0f, 70.0f)
						);
					OutSnapshot.RightHandPalmFacingAlpha =
						FMath::Max(
							OutSnapshot.RightHandPalmFacingAlpha,
							TrackAlpha * TargetAlpha
						);
				}
			}
		}
		else if (Control == TEXT("left_hand.palm_facing"))
		{
			if (Track.TrackType == ELLMMotionTrackType::Anchor)
			{
				OutSnapshot.LeftHandPalmFacingTargetCS =
					BuildAnchorTargetCS(Track, Manifest, Mesh);
				OutSnapshot.LeftHandPalmFacingAlpha = FMath::Max(
					OutSnapshot.LeftHandPalmFacingAlpha,
					TrackAlpha
				);
			}
			else
			{
				FVector TargetLocationWS;
				float TargetAlpha = 0.0f;
				if (
					Mesh &&
					ResolveTargetLocationWS(
						Track.TargetRef,
						TargetMap,
						RuntimeTargetSamples,
						TargetLocationWS,
						TargetAlpha
					)
				)
				{
					OutSnapshot.LeftHandPalmFacingTargetCS =
						Mesh->GetComponentTransform().InverseTransformPosition(
							TargetLocationWS + FVector(0.0f, 0.0f, 70.0f)
						);
					OutSnapshot.LeftHandPalmFacingAlpha =
						FMath::Max(
							OutSnapshot.LeftHandPalmFacingAlpha,
							TrackAlpha * TargetAlpha
						);
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
	case ELLMMotionEnvelope::Sustain:
		{
			constexpr float EdgeFraction = 0.2f;
			const float InAlpha = Smooth01(
				Normalized / EdgeFraction
			);
			const float OutAlpha = Smooth01(
				(1.0f - Normalized) / EdgeFraction
			);
			return FMath::Min(InAlpha, OutAlpha);
		}
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
	const TMap<FString, FLLMNPCTargetRuntimeSample>* RuntimeTargetSamples,
	const FLLMNPCResolvedMotionModifiers* ResolvedModifiers,
	float& OutTargetAlpha,
	bool bLeftHand
)
{
	OutTargetAlpha = 0.0f;
	if (!Mesh)
	{
		return FVector::ZeroVector;
	}

	FVector TargetLocationWS;
	if (!ResolveTargetLocationWS(
		Track.TargetRef,
		TargetMap,
		RuntimeTargetSamples,
		TargetLocationWS,
		OutTargetAlpha
	))
	{
		return FVector::ZeroVector;
	}

	FVector TargetCS = Mesh->GetComponentTransform().InverseTransformPosition(
		TargetLocationWS + FVector(0.0f, 0.0f, 60.0f)
	);

	const FVector ShoulderCS = GetBoneLocationCS(
		Mesh,
		bLeftHand ? FName(TEXT("upperarm_l")) : FName(TEXT("upperarm_r")),
		FVector(0.0f, bLeftHand ? -25.0f : 25.0f, 80.0f)
	);
	if (ResolvedModifiers)
	{
		TargetCS.Z =
			ShoulderCS.Z +
			(TargetCS.Z - ShoulderCS.Z) *
				ResolvedModifiers->HeightScale;
		TargetCS.Y =
			ShoulderCS.Y +
			(TargetCS.Y - ShoulderCS.Y) *
				ResolvedModifiers->LateralScale;
	}
	const FVector Direction = (TargetCS - ShoulderCS).GetSafeNormal();
	const float ReachScale = ResolvedModifiers
		? ResolvedModifiers->ReachScale
		: 1.0f;
	const float EffectiveReach = FMath::Clamp(
		Track.Reach * ReachScale,
		0.0f,
		1.0f
	);
	const float ReachDistance = FMath::Lerp(
		35.0f,
		90.0f,
		EffectiveReach
	);
	return ShoulderCS + Direction * ReachDistance + Track.Offset;
}

bool FLLMNPCMotionSampler::ResolveTargetLocationWS(
	const FString& TargetRef,
	const TMap<FString, TObjectPtr<AActor>>& TargetMap,
	const TMap<FString, FLLMNPCTargetRuntimeSample>* RuntimeTargetSamples,
	FVector& OutLocationWS,
	float& OutTargetAlpha
)
{
	OutLocationWS = FVector::ZeroVector;
	OutTargetAlpha = 0.0f;
	if (RuntimeTargetSamples)
	{
		if (const FLLMNPCTargetRuntimeSample* Sample =
			RuntimeTargetSamples->Find(TargetRef))
		{
			if (!Sample->bValid || Sample->Alpha <= KINDA_SMALL_NUMBER)
			{
				return false;
			}
			OutLocationWS = Sample->LocationWS;
			OutTargetAlpha = FMath::Clamp(Sample->Alpha, 0.0f, 1.0f);
			return true;
		}
	}

	const TObjectPtr<AActor>* Target = TargetMap.Find(TargetRef);
	if (!Target || !IsValid(Target->Get()))
	{
		return false;
	}
	OutLocationWS = Target->Get()->GetActorLocation();
	OutTargetAlpha = 1.0f;
	return true;
}
