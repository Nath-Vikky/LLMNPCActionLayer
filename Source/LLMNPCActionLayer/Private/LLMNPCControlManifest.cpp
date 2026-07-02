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

FLLMControlDefinition MakeRotationControl(FName ControlId, FName BoneName, float MinValue, float MaxValue)
{
	FLLMControlDefinition Def;
	Def.ControlId = ControlId;
	Def.SolverType = ELLMControlSolverType::AdditiveRotation;
	Def.AllowedTrackTypes = FloatTracks();
	Def.MinValue = MinValue;
	Def.MaxValue = MaxValue;

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
			Result.Add(MakeRotationControl(TEXT("right_upperarm.pitch"), TEXT("upperarm_r"), -65.0f, 65.0f));
			Result.Add(MakeRotationControl(TEXT("right_upperarm.yaw"), TEXT("upperarm_r"), -65.0f, 65.0f));
			Result.Add(MakeRotationControl(TEXT("right_upperarm.roll"), TEXT("upperarm_r"), -65.0f, 65.0f));
			Result.Add(MakeRotationControl(TEXT("right_lowerarm.pitch"), TEXT("lowerarm_r"), -80.0f, 80.0f));
			Result.Add(MakeRotationControl(TEXT("right_lowerarm.yaw"), TEXT("lowerarm_r"), -80.0f, 80.0f));
			Result.Add(MakeRotationControl(TEXT("right_lowerarm.roll"), TEXT("lowerarm_r"), -80.0f, 80.0f));
			Result.Add(MakeRotationControl(TEXT("right_hand.pitch"), TEXT("hand_r"), -70.0f, 70.0f));
			Result.Add(MakeRotationControl(TEXT("right_hand.yaw"), TEXT("hand_r"), -70.0f, 70.0f));
			Result.Add(MakeRotationControl(TEXT("right_hand.roll"), TEXT("hand_r"), -70.0f, 70.0f));

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

			FLLMAnchorDefinition RightWave;
			RightWave.AnchorId = TEXT("right_wave");
			RightWave.BoneName = TEXT("head");
			RightWave.OffsetCS = FVector(-22.0f, 27.0f, -10.0f);
			Result.Add(RightWave);

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
