#include "LLMNPCControlManifest.h"

namespace
{
TArray<ELLMMotionTrackType> FloatTracks()
{
	return {
		ELLMMotionTrackType::Keyframes,
		ELLMMotionTrackType::Oscillator,
		ELLMMotionTrackType::Hold,
		ELLMMotionTrackType::Spring
	};
}

FLLMControlDefinition MakeRotationControl(
	FName ControlId,
	FName BoneName,
	float MinValue,
	float MaxValue,
	bool bAllowRuntimeModel = true
)
{
	FLLMControlDefinition Def;
	Def.ControlId = ControlId;
	Def.SolverType = ELLMControlSolverType::AdditiveRotation;
	Def.AllowedTrackTypes = FloatTracks();
	Def.MinValue = MinValue;
	Def.MaxValue = MaxValue;
	Def.bAllowRuntimeModel = bAllowRuntimeModel;
	Def.bAllowLLM = bAllowRuntimeModel;

	FLLMWeightedBone WeightedBone;
	WeightedBone.BoneName = BoneName;
	WeightedBone.Weight = 1.0f;
	Def.WeightedBones.Add(WeightedBone);

	return Def;
}

const TArray<FLLMControlDefinition>& BuiltInControls()
{
	static const TArray<FLLMControlDefinition> Controls =
		[]()
		{
			TArray<FLLMControlDefinition> Result;

			Result.Add(MakeRotationControl(TEXT("head.pitch"), TEXT("head"), -30.0f, 30.0f));
			Result.Add(MakeRotationControl(TEXT("head.yaw"), TEXT("head"), -35.0f, 35.0f));
			Result.Add(MakeRotationControl(TEXT("head.roll"), TEXT("head"), -25.0f, 25.0f));
			Result.Add(MakeRotationControl(TEXT("chest.pitch"), TEXT("spine_03"), -20.0f, 20.0f));
			Result.Add(MakeRotationControl(TEXT("chest.yaw"), TEXT("spine_03"), -25.0f, 25.0f));
			Result.Add(MakeRotationControl(TEXT("chest.roll"), TEXT("spine_03"), -18.0f, 18.0f));
			Result.Add(MakeRotationControl(TEXT("right_upperarm.pitch"), TEXT("upperarm_r"), -125.0f, 125.0f, false));
			Result.Add(MakeRotationControl(TEXT("right_upperarm.yaw"), TEXT("upperarm_r"), -125.0f, 125.0f, false));
			Result.Add(MakeRotationControl(TEXT("right_upperarm.roll"), TEXT("upperarm_r"), -125.0f, 125.0f, false));
			Result.Add(MakeRotationControl(TEXT("right_lowerarm.pitch"), TEXT("lowerarm_r"), -125.0f, 125.0f, false));
			Result.Add(MakeRotationControl(TEXT("right_lowerarm.yaw"), TEXT("lowerarm_r"), -125.0f, 125.0f, false));
			Result.Add(MakeRotationControl(TEXT("right_lowerarm.roll"), TEXT("lowerarm_r"), -125.0f, 125.0f, false));
			Result.Add(MakeRotationControl(TEXT("right_hand.pitch"), TEXT("hand_r"), -95.0f, 95.0f, false));
			Result.Add(MakeRotationControl(TEXT("right_hand.yaw"), TEXT("hand_r"), -95.0f, 95.0f, false));
			Result.Add(MakeRotationControl(TEXT("right_hand.roll"), TEXT("hand_r"), -95.0f, 95.0f, false));

			FLLMControlDefinition RightHandIK;
			RightHandIK.ControlId = TEXT("right_hand.ik");
			RightHandIK.SolverType = ELLMControlSolverType::TwoBoneIK;
			RightHandIK.AllowedTrackTypes = {
				ELLMMotionTrackType::Anchor,
				ELLMMotionTrackType::IKReach,
				ELLMMotionTrackType::Keyframes
			};
			RightHandIK.MinValue = 0.0f;
			RightHandIK.MaxValue = 1.0f;
			RightHandIK.bRequiresTarget = false;
			RightHandIK.IKChain.RootBone = TEXT("upperarm_r");
			RightHandIK.IKChain.MidBone = TEXT("lowerarm_r");
			RightHandIK.IKChain.EndBone = TEXT("hand_r");
			RightHandIK.IKChain.PoleVectorCS = FVector(0.0f, 45.0f, 0.0f);
			RightHandIK.IKChain.MaxReach = 95.0f;
			Result.Add(RightHandIK);

			FLLMControlDefinition LeftHandIK = RightHandIK;
			LeftHandIK.ControlId = TEXT("left_hand.ik");
			LeftHandIK.IKChain.RootBone = TEXT("upperarm_l");
			LeftHandIK.IKChain.MidBone = TEXT("lowerarm_l");
			LeftHandIK.IKChain.EndBone = TEXT("hand_l");
			LeftHandIK.IKChain.PoleVectorCS = FVector(0.0f, -45.0f, 0.0f);
			Result.Add(LeftHandIK);

			FLLMControlDefinition Gaze;
			Gaze.ControlId = TEXT("gaze.target");
			Gaze.SolverType = ELLMControlSolverType::LookAt;
			Gaze.AllowedTrackTypes = { ELLMMotionTrackType::LookAt };
			Gaze.MinValue = 0.0f;
			Gaze.MaxValue = 1.0f;
			Gaze.bRequiresTarget = true;
			Result.Add(Gaze);

			FLLMControlDefinition RightHandPalmTarget = Gaze;
			RightHandPalmTarget.ControlId = TEXT("right_hand.palm_target");
			RightHandPalmTarget.bRequiresTarget = true;
			Result.Add(RightHandPalmTarget);

			FLLMControlDefinition LeftHandPalmTarget = RightHandPalmTarget;
			LeftHandPalmTarget.ControlId = TEXT("left_hand.palm_target");
			Result.Add(LeftHandPalmTarget);

			FLLMControlDefinition RightHandOffsetX;
			RightHandOffsetX.ControlId = TEXT("right_hand.local_offset.x");
			RightHandOffsetX.SolverType = ELLMControlSolverType::LocalOffset;
			RightHandOffsetX.AllowedTrackTypes = FloatTracks();
			RightHandOffsetX.MinValue = -80.0f;
			RightHandOffsetX.MaxValue = 80.0f;
			Result.Add(RightHandOffsetX);

			FLLMControlDefinition RightHandOffsetY = RightHandOffsetX;
			RightHandOffsetY.ControlId = TEXT("right_hand.local_offset.y");
			Result.Add(RightHandOffsetY);

			FLLMControlDefinition RightHandOffsetZ = RightHandOffsetX;
			RightHandOffsetZ.ControlId = TEXT("right_hand.local_offset.z");
			Result.Add(RightHandOffsetZ);

			FLLMControlDefinition LeftHandOffsetX = RightHandOffsetX;
			LeftHandOffsetX.ControlId = TEXT("left_hand.local_offset.x");
			Result.Add(LeftHandOffsetX);

			FLLMControlDefinition LeftHandOffsetY = RightHandOffsetX;
			LeftHandOffsetY.ControlId = TEXT("left_hand.local_offset.y");
			Result.Add(LeftHandOffsetY);

			FLLMControlDefinition LeftHandOffsetZ = RightHandOffsetX;
			LeftHandOffsetZ.ControlId = TEXT("left_hand.local_offset.z");
			Result.Add(LeftHandOffsetZ);

			FLLMControlDefinition FingersOpen;
			FingersOpen.ControlId = TEXT("right_fingers.open");
			FingersOpen.SolverType = ELLMControlSolverType::FingerPoseBlend;
			FingersOpen.AllowedTrackTypes = {
				ELLMMotionTrackType::Keyframes,
				ELLMMotionTrackType::Hold
			};
			FingersOpen.MinValue = 0.0f;
			FingersOpen.MaxValue = 1.0f;
			Result.Add(FingersOpen);

			FLLMControlDefinition FingersPoint = FingersOpen;
			FingersPoint.ControlId = TEXT("right_fingers.point");
			Result.Add(FingersPoint);

			FLLMControlDefinition LeftFingersOpen = FingersOpen;
			LeftFingersOpen.ControlId = TEXT("left_fingers.open");
			Result.Add(LeftFingersOpen);

			FLLMControlDefinition LeftFingersPoint = FingersOpen;
			LeftFingersPoint.ControlId = TEXT("left_fingers.point");
			Result.Add(LeftFingersPoint);

			return Result;
		}();

	return Controls;
}

const TArray<FLLMAnchorDefinition>& BuiltInAnchors()
{
	static const TArray<FLLMAnchorDefinition> Anchors =
		[]()
		{
			TArray<FLLMAnchorDefinition> Result;

			FLLMAnchorDefinition HeadRight;
			HeadRight.AnchorId = TEXT("head_right");
			HeadRight.BoneName = TEXT("head");
			HeadRight.OffsetCS = FVector(25.0f, 35.0f, 10.0f);
			Result.Add(HeadRight);

			FLLMAnchorDefinition HeadLeft = HeadRight;
			HeadLeft.AnchorId = TEXT("head_left");
			HeadLeft.OffsetCS.Y *= -1.0f;
			Result.Add(HeadLeft);

			FLLMAnchorDefinition RightWave;
			RightWave.AnchorId = TEXT("right_wave");
			RightWave.BoneName = TEXT("head");
			RightWave.OffsetCS = FVector(-22.0f, 27.0f, -10.0f);
			Result.Add(RightWave);

			FLLMAnchorDefinition LeftWave = RightWave;
			LeftWave.AnchorId = TEXT("left_wave");
			LeftWave.OffsetCS.Y *= -1.0f;
			Result.Add(LeftWave);

			FLLMAnchorDefinition ChestFront;
			ChestFront.AnchorId = TEXT("chest_front");
			ChestFront.BoneName = TEXT("spine_03");
			ChestFront.OffsetCS = FVector(35.0f, 20.0f, 5.0f);
			Result.Add(ChestFront);

			return Result;
		}();

	return Anchors;
}
}

const FLLMControlDefinition* ULLMNPCControlManifest::FindControl(FName ControlId) const
{
	if (const FLLMControlDefinition* Def = Controls.FindByPredicate(
		[ControlId](const FLLMControlDefinition& Candidate)
		{
			return Candidate.ControlId == ControlId;
		}))
	{
		return Def;
	}

	return FindBuiltInControl(ControlId);
}

const FLLMAnchorDefinition* ULLMNPCControlManifest::FindAnchor(FName AnchorId) const
{
	if (const FLLMAnchorDefinition* Def = Anchors.FindByPredicate(
		[AnchorId](const FLLMAnchorDefinition& Candidate)
		{
			return Candidate.AnchorId == AnchorId;
		}))
	{
		return Def;
	}

	return FindBuiltInAnchor(AnchorId);
}

const FLLMControlDefinition* ULLMNPCControlManifest::FindBuiltInControl(FName ControlId)
{
	return BuiltInControls().FindByPredicate(
		[ControlId](const FLLMControlDefinition& Candidate)
		{
			return Candidate.ControlId == ControlId;
		}
	);
}

const FLLMAnchorDefinition* ULLMNPCControlManifest::FindBuiltInAnchor(FName AnchorId)
{
	return BuiltInAnchors().FindByPredicate(
		[AnchorId](const FLLMAnchorDefinition& Candidate)
		{
			return Candidate.AnchorId == AnchorId;
		}
	);
}

const TArray<FLLMControlDefinition>& ULLMNPCControlManifest::GetBuiltInControls()
{
	return BuiltInControls();
}

const TArray<FLLMAnchorDefinition>& ULLMNPCControlManifest::GetBuiltInAnchors()
{
	return BuiltInAnchors();
}
