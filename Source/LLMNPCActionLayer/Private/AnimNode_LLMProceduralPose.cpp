#include "AnimNode_LLMProceduralPose.h"

#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "LLMNPCMotionMirror.h"
#include "Math/RotationMatrix.h"
#include "TwoBoneIK.h"

namespace
{
void AddOrReplaceBoneTransform(TArray<FBoneTransform>& OutBoneTransforms, FCompactPoseBoneIndex BoneIndex, const FTransform& Transform)
{
	if (FBoneTransform* Existing = OutBoneTransforms.FindByPredicate(
		[BoneIndex](const FBoneTransform& BoneTransform)
		{
			return BoneTransform.BoneIndex == BoneIndex;
		}))
	{
		Existing->Transform = Transform;
		return;
	}

	OutBoneTransforms.Add(FBoneTransform(BoneIndex, Transform));
}

FTransform GetCurrentBoneTransformCS(
	FComponentSpacePoseContext& Output,
	const TArray<FBoneTransform>& OutBoneTransforms,
	FCompactPoseBoneIndex BoneIndex
)
{
	if (const FBoneTransform* Existing = OutBoneTransforms.FindByPredicate(
		[BoneIndex](const FBoneTransform& BoneTransform)
		{
			return BoneTransform.BoneIndex == BoneIndex;
		}))
	{
		return Existing->Transform;
	}

	return Output.Pose.GetComponentSpaceTransform(BoneIndex);
}

bool HasBoneTransform(const TArray<FBoneTransform>& OutBoneTransforms, FCompactPoseBoneIndex BoneIndex)
{
	return OutBoneTransforms.ContainsByPredicate(
		[BoneIndex](const FBoneTransform& BoneTransform)
		{
			return BoneTransform.BoneIndex == BoneIndex;
		});
}

FTransform ApplyLocalRotationDelta(
	const FTransform& LocalTM,
	const FRotator& DeltaRotation,
	float Alpha
)
{
	if (DeltaRotation.IsNearlyZero())
	{
		return LocalTM;
	}

	FTransform NewLocalTM = LocalTM;
	const FQuat FullDeltaQuat = FQuat(DeltaRotation);
	const FQuat DeltaQuat = Alpha >= 1.0f - KINDA_SMALL_NUMBER
		? FullDeltaQuat
		: FQuat::Slerp(FQuat::Identity, FullDeltaQuat, FMath::Clamp(Alpha, 0.0f, 1.0f)).GetNormalized();
	NewLocalTM.SetRotation(DeltaQuat * NewLocalTM.GetRotation());
	NewLocalTM.NormalizeRotation();
	return NewLocalTM;
}

FCompactPoseBoneIndex ResolveBoneIndex(
	const FBoneContainer& BoneContainer,
	const FBoneReference& Bone,
	FName BoundBoneName,
	FName FallbackBoneName
)
{
	if (!BoundBoneName.IsNone())
	{
		const int32 BoundMeshPoseIndex = BoneContainer.GetPoseBoneIndexForBoneName(BoundBoneName);
		if (BoundMeshPoseIndex != INDEX_NONE)
		{
			return BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoundMeshPoseIndex));
		}
	}

	if (Bone.IsValidToEvaluate(BoneContainer))
	{
		return Bone.GetCompactPoseIndex(BoneContainer);
	}

	const FName BoneName = Bone.BoneName.IsNone() ? FallbackBoneName : Bone.BoneName;
	if (BoneName.IsNone())
	{
		return FCompactPoseBoneIndex(INDEX_NONE);
	}

	const int32 MeshPoseIndex = BoneContainer.GetPoseBoneIndexForBoneName(BoneName);
	if (MeshPoseIndex == INDEX_NONE)
	{
		return FCompactPoseBoneIndex(INDEX_NONE);
	}

	return BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshPoseIndex));
}

bool IsValidCompactPoseBoneIndex(const FCompactPoseBoneIndex BoneIndex)
{
	return BoneIndex.GetInt() != INDEX_NONE;
}

FName GetSnapshotBinding(bool bUseBindings, const FLLMNPCPoseBoneBindings& Bindings, FName BoneName)
{
	return bUseBindings && !Bindings.ProfileId.IsNone() ? BoneName : NAME_None;
}

FName GetSnapshotFingerBinding(
	bool bUseBindings,
	const FLLMNPCPoseBoneBindings& Bindings,
	const TArray<FName>& FingerBones,
	int32 Index
)
{
	return bUseBindings && !Bindings.ProfileId.IsNone() && FingerBones.IsValidIndex(Index)
		? FingerBones[Index]
		: NAME_None;
}

FRotator ResolveCalibratedRotation(
	const FRotator& SemanticRotation,
	const FLLMNPCResolvedAxisBasis& Basis,
	bool bApplyCalibration
)
{
	if (!bApplyCalibration)
	{
		return SemanticRotation;
	}

	const float Pitch = FMath::Clamp(
		SemanticRotation.Pitch,
		Basis.MinAdditiveRotation.Pitch,
		Basis.MaxAdditiveRotation.Pitch);
	const float Yaw = FMath::Clamp(
		SemanticRotation.Yaw,
		Basis.MinAdditiveRotation.Yaw,
		Basis.MaxAdditiveRotation.Yaw);
	const float Roll = FMath::Clamp(
		SemanticRotation.Roll,
		Basis.MinAdditiveRotation.Roll,
		Basis.MaxAdditiveRotation.Roll);
	const FQuat PitchQuat(Basis.PitchAxis.GetSafeNormal(), FMath::DegreesToRadians(Pitch));
	const FQuat YawQuat(Basis.YawAxis.GetSafeNormal(), FMath::DegreesToRadians(Yaw));
	const FQuat RollQuat(Basis.RollAxis.GetSafeNormal(), FMath::DegreesToRadians(Roll));
	return (RollQuat * YawQuat * PitchQuat).GetNormalized().Rotator();
}

bool RebaseBoneToCurrentParentCS(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	FCompactPoseBoneIndex BoneIndex
)
{
	if (!IsValidCompactPoseBoneIndex(BoneIndex))
	{
		return false;
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex ParentIndex = BoneContainer.GetParentBoneIndex(BoneIndex);
	if (!IsValidCompactPoseBoneIndex(ParentIndex))
	{
		return false;
	}

	const FTransform OriginalParentTM = Output.Pose.GetComponentSpaceTransform(ParentIndex);
	const FTransform OriginalBoneTM = Output.Pose.GetComponentSpaceTransform(BoneIndex);
	const FTransform CurrentParentTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, ParentIndex);

	FTransform LocalTM = OriginalBoneTM.GetRelativeTransform(OriginalParentTM);
	FTransform NewBoneTM = LocalTM * CurrentParentTM;
	NewBoneTM.NormalizeRotation();
	AddOrReplaceBoneTransform(OutBoneTransforms, BoneIndex, NewBoneTM);
	return true;
}

void RebaseDescendantsToCurrentHierarchyCS(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	FCompactPoseBoneIndex RootIndex
)
{
	if (!IsValidCompactPoseBoneIndex(RootIndex))
	{
		return;
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	for (int32 CompactIndex = 0; CompactIndex < BoneContainer.GetCompactPoseNumBones(); ++CompactIndex)
	{
		const FCompactPoseBoneIndex CandidateIndex(CompactIndex);
		if (CandidateIndex == RootIndex)
		{
			continue;
		}

		FCompactPoseBoneIndex ParentIndex = BoneContainer.GetParentBoneIndex(CandidateIndex);
		while (IsValidCompactPoseBoneIndex(ParentIndex))
		{
			if (ParentIndex == RootIndex)
			{
				RebaseBoneToCurrentParentCS(Output, OutBoneTransforms, CandidateIndex);
				break;
			}
			ParentIndex = BoneContainer.GetParentBoneIndex(ParentIndex);
		}
	}
}
}

FAnimNode_LLMProceduralPose::FAnimNode_LLMProceduralPose()
{
	HeadBone.BoneName = TEXT("head");
	ChestBone.BoneName = TEXT("spine_03");
	RightUpperArmBone.BoneName = TEXT("upperarm_r");
	RightLowerArmBone.BoneName = TEXT("lowerarm_r");
	RightHandBone.BoneName = TEXT("hand_r");
	RightThumb01Bone.BoneName = TEXT("thumb_01_r");
	RightThumb02Bone.BoneName = TEXT("thumb_02_r");
	RightThumb03Bone.BoneName = TEXT("thumb_03_r");
	RightIndex01Bone.BoneName = TEXT("index_01_r");
	RightIndex02Bone.BoneName = TEXT("index_02_r");
	RightIndex03Bone.BoneName = TEXT("index_03_r");
	RightMiddle01Bone.BoneName = TEXT("middle_01_r");
	RightMiddle02Bone.BoneName = TEXT("middle_02_r");
	RightMiddle03Bone.BoneName = TEXT("middle_03_r");
	RightRing01Bone.BoneName = TEXT("ring_01_r");
	RightRing02Bone.BoneName = TEXT("ring_02_r");
	RightRing03Bone.BoneName = TEXT("ring_03_r");
	RightPinky01Bone.BoneName = TEXT("pinky_01_r");
	RightPinky02Bone.BoneName = TEXT("pinky_02_r");
	RightPinky03Bone.BoneName = TEXT("pinky_03_r");
	LeftUpperArmBone.BoneName = TEXT("upperarm_l");
	LeftLowerArmBone.BoneName = TEXT("lowerarm_l");
	LeftHandBone.BoneName = TEXT("hand_l");
	LeftThumb01Bone.BoneName = TEXT("thumb_01_l");
	LeftThumb02Bone.BoneName = TEXT("thumb_02_l");
	LeftThumb03Bone.BoneName = TEXT("thumb_03_l");
	LeftIndex01Bone.BoneName = TEXT("index_01_l");
	LeftIndex02Bone.BoneName = TEXT("index_02_l");
	LeftIndex03Bone.BoneName = TEXT("index_03_l");
	LeftMiddle01Bone.BoneName = TEXT("middle_01_l");
	LeftMiddle02Bone.BoneName = TEXT("middle_02_l");
	LeftMiddle03Bone.BoneName = TEXT("middle_03_l");
	LeftRing01Bone.BoneName = TEXT("ring_01_l");
	LeftRing02Bone.BoneName = TEXT("ring_02_l");
	LeftRing03Bone.BoneName = TEXT("ring_03_l");
	LeftPinky01Bone.BoneName = TEXT("pinky_01_l");
	LeftPinky02Bone.BoneName = TEXT("pinky_02_l");
	LeftPinky03Bone.BoneName = TEXT("pinky_03_l");
	Alpha = 1.0f;
}

void FAnimNode_LLMProceduralPose::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);
}

void FAnimNode_LLMProceduralPose::GatherDebugData(FNodeDebugData& DebugData)
{
	FString DebugLine = DebugData.GetNodeName(this);
	DebugLine += TEXT("(LLM Procedural Pose)");
	DebugData.AddDebugItem(DebugLine);
	ComponentPose.GatherDebugData(DebugData);
}

void FAnimNode_LLMProceduralPose::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	HeadBone.Initialize(RequiredBones);
	ChestBone.Initialize(RequiredBones);
	RightUpperArmBone.Initialize(RequiredBones);
	RightLowerArmBone.Initialize(RequiredBones);
	RightHandBone.Initialize(RequiredBones);
	RightThumb01Bone.Initialize(RequiredBones);
	RightThumb02Bone.Initialize(RequiredBones);
	RightThumb03Bone.Initialize(RequiredBones);
	RightIndex01Bone.Initialize(RequiredBones);
	RightIndex02Bone.Initialize(RequiredBones);
	RightIndex03Bone.Initialize(RequiredBones);
	RightMiddle01Bone.Initialize(RequiredBones);
	RightMiddle02Bone.Initialize(RequiredBones);
	RightMiddle03Bone.Initialize(RequiredBones);
	RightRing01Bone.Initialize(RequiredBones);
	RightRing02Bone.Initialize(RequiredBones);
	RightRing03Bone.Initialize(RequiredBones);
	RightPinky01Bone.Initialize(RequiredBones);
	RightPinky02Bone.Initialize(RequiredBones);
	RightPinky03Bone.Initialize(RequiredBones);
	LeftUpperArmBone.Initialize(RequiredBones);
	LeftLowerArmBone.Initialize(RequiredBones);
	LeftHandBone.Initialize(RequiredBones);
	LeftThumb01Bone.Initialize(RequiredBones);
	LeftThumb02Bone.Initialize(RequiredBones);
	LeftThumb03Bone.Initialize(RequiredBones);
	LeftIndex01Bone.Initialize(RequiredBones);
	LeftIndex02Bone.Initialize(RequiredBones);
	LeftIndex03Bone.Initialize(RequiredBones);
	LeftMiddle01Bone.Initialize(RequiredBones);
	LeftMiddle02Bone.Initialize(RequiredBones);
	LeftMiddle03Bone.Initialize(RequiredBones);
	LeftRing01Bone.Initialize(RequiredBones);
	LeftRing02Bone.Initialize(RequiredBones);
	LeftRing03Bone.Initialize(RequiredBones);
	LeftPinky01Bone.Initialize(RequiredBones);
	LeftPinky02Bone.Initialize(RequiredBones);
	LeftPinky03Bone.Initialize(RequiredBones);
}

bool FAnimNode_LLMProceduralPose::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	return IsValidCompactPoseBoneIndex(ResolveBoneIndex(RequiredBones, HeadBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.Head), TEXT("head"))) ||
		IsValidCompactPoseBoneIndex(ResolveBoneIndex(RequiredBones, ChestBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.Chest), TEXT("spine_03"))) ||
		HasAnyRightFingerBone(RequiredBones) ||
		HasAnyLeftFingerBone(RequiredBones) ||
		(
			bEnableRightArmIK &&
			IsValidCompactPoseBoneIndex(ResolveBoneIndex(RequiredBones, RightUpperArmBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightUpperArm), TEXT("upperarm_r"))) &&
			IsValidCompactPoseBoneIndex(ResolveBoneIndex(RequiredBones, RightLowerArmBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightLowerArm), TEXT("lowerarm_r"))) &&
			IsValidCompactPoseBoneIndex(ResolveBoneIndex(RequiredBones, RightHandBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightHand), TEXT("hand_r")))
		) ||
		(
			bEnableLeftArmIK &&
			IsValidCompactPoseBoneIndex(ResolveBoneIndex(RequiredBones, LeftUpperArmBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftUpperArm), TEXT("upperarm_l"))) &&
			IsValidCompactPoseBoneIndex(ResolveBoneIndex(RequiredBones, LeftLowerArmBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftLowerArm), TEXT("lowerarm_l"))) &&
			IsValidCompactPoseBoneIndex(ResolveBoneIndex(RequiredBones, LeftHandBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftHand), TEXT("hand_l")))
		);
}

void FAnimNode_LLMProceduralPose::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms
)
{
	check(OutBoneTransforms.Num() == 0);

	const float NodeAlpha = FMath::Clamp(ActualAlpha, 0.0f, 1.0f);
	if (NodeAlpha <= KINDA_SMALL_NUMBER || Snapshot.GlobalAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	const bool bApplyAxisCalibration = bUseBindings && Bindings.bApplyAxisCalibration;
	// Preserve the calibrated Manny convention until a profile explicitly enables axis remapping.
	FRotator HeadRotation = bApplyAxisCalibration
		? ResolveCalibratedRotation(
			FRotator(Snapshot.HeadPitch, Snapshot.HeadYaw, Snapshot.HeadRoll),
			Bindings.HeadAxis,
			true)
		: FRotator(0.0f, Snapshot.HeadYaw, Snapshot.HeadPitch);
	FRotator ChestRotation = ResolveCalibratedRotation(
		FRotator(Snapshot.ChestPitch, Snapshot.ChestYaw, Snapshot.ChestRoll),
		Bindings.ChestAxis,
		bApplyAxisCalibration);
	AddGazeToRotations(Output, HeadRotation, ChestRotation);

	ApplyAdditiveRotationCS(Output, OutBoneTransforms, HeadBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.Head), HeadRotation, NodeAlpha);
	ApplyAdditiveRotationCS(Output, OutBoneTransforms, ChestBone, GetSnapshotBinding(bUseBindings, Bindings, Bindings.Chest), ChestRotation, NodeAlpha);

	if (bEnableRightArmIK && Snapshot.RightHandIKAlpha > KINDA_SMALL_NUMBER)
	{
		ApplySimpleRightArmIK(Output, OutBoneTransforms);
	}
	if (bEnableLeftArmIK && Snapshot.LeftHandIKAlpha > KINDA_SMALL_NUMBER)
	{
		ApplySimpleLeftArmIK(Output, OutBoneTransforms);
	}

	ApplyRightArmAdditiveRotationsLocal(Output, OutBoneTransforms, NodeAlpha);
	ApplyLeftArmAdditiveRotationsLocal(Output, OutBoneTransforms, NodeAlpha);
	PropagateRightHandChildrenCS(Output, OutBoneTransforms);
	ApplyRightFingerPoseLocal(Output, OutBoneTransforms, NodeAlpha);
	PropagateLeftHandChildrenCS(Output, OutBoneTransforms);
	ApplyLeftFingerPoseLocal(Output, OutBoneTransforms, NodeAlpha);

	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

void FAnimNode_LLMProceduralPose::ApplyAdditiveRotationCS(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	const FBoneReference& Bone,
	FName BoundBoneName,
	const FRotator& Rotation,
	float InAlpha
) const
{
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex BoneIndex = ResolveBoneIndex(BoneContainer, Bone, BoundBoneName, NAME_None);
	if (!IsValidCompactPoseBoneIndex(BoneIndex))
	{
		return;
	}

	FTransform NewBoneTM = Output.Pose.GetComponentSpaceTransform(BoneIndex);
	const FQuat AdditiveQuat = FQuat(Rotation * InAlpha);
	NewBoneTM.SetRotation(AdditiveQuat * NewBoneTM.GetRotation());
	NewBoneTM.NormalizeRotation();
	AddOrReplaceBoneTransform(OutBoneTransforms, BoneIndex, NewBoneTM);
}

void FAnimNode_LLMProceduralPose::ApplySimpleRightArmIK(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms
) const
{
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	const FCompactPoseBoneIndex UpperIndex = ResolveBoneIndex(
		BoneContainer, RightUpperArmBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightUpperArm), TEXT("upperarm_r"));
	const FCompactPoseBoneIndex LowerIndex = ResolveBoneIndex(
		BoneContainer, RightLowerArmBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightLowerArm), TEXT("lowerarm_r"));
	const FCompactPoseBoneIndex HandIndex = ResolveBoneIndex(
		BoneContainer, RightHandBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightHand), TEXT("hand_r"));
	if (!IsValidCompactPoseBoneIndex(UpperIndex) ||
		!IsValidCompactPoseBoneIndex(LowerIndex) ||
		!IsValidCompactPoseBoneIndex(HandIndex))
	{
		return;
	}

	const float IKAlpha = FMath::Clamp(Snapshot.RightHandIKAlpha * ActualAlpha, 0.0f, 1.0f);
	if (IKAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FTransform UpperTM = Output.Pose.GetComponentSpaceTransform(UpperIndex);
	FTransform LowerTM = Output.Pose.GetComponentSpaceTransform(LowerIndex);
	FTransform HandTM = Output.Pose.GetComponentSpaceTransform(HandIndex);

	const FTransform OriginalUpperTM = UpperTM;
	const FTransform OriginalLowerTM = LowerTM;
	const FTransform OriginalHandTM = HandTM;

	const FVector RootPos = UpperTM.GetLocation();
	const FVector JointTarget = RootPos + FVector(0.0f, 45.0f, 0.0f);
	const FVector DesiredPos = Snapshot.RightHandIKTargetCS;

	AnimationCore::SolveTwoBoneIK(
		UpperTM,
		LowerTM,
		HandTM,
		JointTarget,
		DesiredPos,
		false,
		1.0f,
		1.2f
	);

	const float PalmAlpha = FMath::Clamp(Snapshot.RightHandPalmAlpha * ActualAlpha, 0.0f, 1.0f);
	if (PalmAlpha > KINDA_SMALL_NUMBER && !Snapshot.RightHandPalmTargetCS.IsNearlyZero())
	{
		const FVector PalmDirection = (Snapshot.RightHandPalmTargetCS - HandTM.GetLocation()).GetSafeNormal();
		if (!PalmDirection.IsNearlyZero())
		{
			const FQuat PalmTargetRotation =
				FRotationMatrix::MakeFromXZ(PalmDirection, FVector::UpVector).ToQuat() *
				RightHandPalmRotationOffset.Quaternion();
			HandTM.SetRotation(FQuat::Slerp(HandTM.GetRotation(), PalmTargetRotation, PalmAlpha).GetNormalized());
		}
	}

	UpperTM.Blend(OriginalUpperTM, UpperTM, IKAlpha);
	LowerTM.Blend(OriginalLowerTM, LowerTM, IKAlpha);
	HandTM.Blend(OriginalHandTM, HandTM, IKAlpha);

	AddOrReplaceBoneTransform(OutBoneTransforms, UpperIndex, UpperTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, LowerIndex, LowerTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, HandIndex, HandTM);
}

void FAnimNode_LLMProceduralPose::ApplySimpleLeftArmIK(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms
) const
{
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	const FCompactPoseBoneIndex UpperIndex = ResolveBoneIndex(
		BoneContainer, LeftUpperArmBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftUpperArm), TEXT("upperarm_l"));
	const FCompactPoseBoneIndex LowerIndex = ResolveBoneIndex(
		BoneContainer, LeftLowerArmBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftLowerArm), TEXT("lowerarm_l"));
	const FCompactPoseBoneIndex HandIndex = ResolveBoneIndex(
		BoneContainer, LeftHandBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftHand), TEXT("hand_l"));
	if (!IsValidCompactPoseBoneIndex(UpperIndex) ||
		!IsValidCompactPoseBoneIndex(LowerIndex) ||
		!IsValidCompactPoseBoneIndex(HandIndex))
	{
		return;
	}

	const float IKAlpha = FMath::Clamp(Snapshot.LeftHandIKAlpha * ActualAlpha, 0.0f, 1.0f);
	if (IKAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FTransform UpperTM = Output.Pose.GetComponentSpaceTransform(UpperIndex);
	FTransform LowerTM = Output.Pose.GetComponentSpaceTransform(LowerIndex);
	FTransform HandTM = Output.Pose.GetComponentSpaceTransform(HandIndex);
	const FTransform OriginalUpperTM = UpperTM;
	const FTransform OriginalLowerTM = LowerTM;
	const FTransform OriginalHandTM = HandTM;
	const FVector JointTarget = UpperTM.GetLocation() + FVector(0.0f, -45.0f, 0.0f);

	AnimationCore::SolveTwoBoneIK(
		UpperTM,
		LowerTM,
		HandTM,
		JointTarget,
		Snapshot.LeftHandIKTargetCS,
		false,
		1.0f,
		1.2f
	);

	const float PalmAlpha = FMath::Clamp(Snapshot.LeftHandPalmAlpha * ActualAlpha, 0.0f, 1.0f);
	if (PalmAlpha > KINDA_SMALL_NUMBER && !Snapshot.LeftHandPalmTargetCS.IsNearlyZero())
	{
		const FVector PalmDirection = (Snapshot.LeftHandPalmTargetCS - HandTM.GetLocation()).GetSafeNormal();
		if (!PalmDirection.IsNearlyZero())
		{
			const FQuat PalmTargetRotation =
				FRotationMatrix::MakeFromXZ(PalmDirection, FVector::UpVector).ToQuat() *
				LeftHandPalmRotationOffset.Quaternion();
			HandTM.SetRotation(FQuat::Slerp(HandTM.GetRotation(), PalmTargetRotation, PalmAlpha).GetNormalized());
		}
	}

	UpperTM.Blend(OriginalUpperTM, UpperTM, IKAlpha);
	LowerTM.Blend(OriginalLowerTM, LowerTM, IKAlpha);
	HandTM.Blend(OriginalHandTM, HandTM, IKAlpha);
	AddOrReplaceBoneTransform(OutBoneTransforms, UpperIndex, UpperTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, LowerIndex, LowerTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, HandIndex, HandTM);
}

void FAnimNode_LLMProceduralPose::ApplyRightArmAdditiveRotationsLocal(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	float InAlpha
) const
{
	if (InAlpha <= KINDA_SMALL_NUMBER ||
		(
			Snapshot.RightUpperArmAdditiveRotation.IsNearlyZero() &&
			Snapshot.RightLowerArmAdditiveRotation.IsNearlyZero() &&
			Snapshot.RightHandAdditiveRotation.IsNearlyZero()
		))
	{
		return;
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	const FCompactPoseBoneIndex UpperIndex = ResolveBoneIndex(
		BoneContainer, RightUpperArmBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightUpperArm), TEXT("upperarm_r"));
	const FCompactPoseBoneIndex LowerIndex = ResolveBoneIndex(
		BoneContainer, RightLowerArmBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightLowerArm), TEXT("lowerarm_r"));
	const FCompactPoseBoneIndex HandIndex = ResolveBoneIndex(
		BoneContainer, RightHandBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightHand), TEXT("hand_r"));
	if (!IsValidCompactPoseBoneIndex(UpperIndex) ||
		!IsValidCompactPoseBoneIndex(LowerIndex) ||
		!IsValidCompactPoseBoneIndex(HandIndex))
	{
		return;
	}

	const FCompactPoseBoneIndex UpperParentIndex = BoneContainer.GetParentBoneIndex(UpperIndex);

	const FTransform UpperParentTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, UpperParentIndex);
	const FTransform OriginalUpperTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, UpperIndex);
	const FTransform OriginalLowerTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, LowerIndex);
	const FTransform OriginalHandTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, HandIndex);

	FTransform UpperLocalTM = OriginalUpperTM.GetRelativeTransform(UpperParentTM);
	FTransform LowerLocalTM = OriginalLowerTM.GetRelativeTransform(OriginalUpperTM);
	FTransform HandLocalTM = OriginalHandTM.GetRelativeTransform(OriginalLowerTM);
	const bool bApplyAxisCalibration = bUseBindings && Bindings.bApplyAxisCalibration;
	const FRotator UpperRotation = ResolveCalibratedRotation(
		Snapshot.RightUpperArmAdditiveRotation, Bindings.RightUpperArmAxis, bApplyAxisCalibration);
	const FRotator LowerRotation = ResolveCalibratedRotation(
		Snapshot.RightLowerArmAdditiveRotation, Bindings.RightLowerArmAxis, bApplyAxisCalibration);
	const FRotator HandRotation = ResolveCalibratedRotation(
		Snapshot.RightHandAdditiveRotation, Bindings.RightHandAxis, bApplyAxisCalibration);

	UpperLocalTM = ApplyLocalRotationDelta(UpperLocalTM, UpperRotation, InAlpha);
	LowerLocalTM = ApplyLocalRotationDelta(LowerLocalTM, LowerRotation, InAlpha);
	HandLocalTM = ApplyLocalRotationDelta(HandLocalTM, HandRotation, InAlpha);

	FTransform UpperTM = UpperLocalTM * UpperParentTM;
	UpperTM.NormalizeRotation();

	FTransform LowerTM = LowerLocalTM * UpperTM;
	LowerTM.NormalizeRotation();

	FTransform HandTM = HandLocalTM * LowerTM;
	HandTM.NormalizeRotation();

	AddOrReplaceBoneTransform(OutBoneTransforms, UpperIndex, UpperTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, LowerIndex, LowerTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, HandIndex, HandTM);
}

void FAnimNode_LLMProceduralPose::ApplyLeftArmAdditiveRotationsLocal(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	float InAlpha
) const
{
	if (InAlpha <= KINDA_SMALL_NUMBER ||
		(
			Snapshot.LeftUpperArmAdditiveRotation.IsNearlyZero() &&
			Snapshot.LeftLowerArmAdditiveRotation.IsNearlyZero() &&
			Snapshot.LeftHandAdditiveRotation.IsNearlyZero()
		))
	{
		return;
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	const FCompactPoseBoneIndex UpperIndex = ResolveBoneIndex(
		BoneContainer, LeftUpperArmBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftUpperArm), TEXT("upperarm_l"));
	const FCompactPoseBoneIndex LowerIndex = ResolveBoneIndex(
		BoneContainer, LeftLowerArmBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftLowerArm), TEXT("lowerarm_l"));
	const FCompactPoseBoneIndex HandIndex = ResolveBoneIndex(
		BoneContainer, LeftHandBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftHand), TEXT("hand_l"));
	if (!IsValidCompactPoseBoneIndex(UpperIndex) ||
		!IsValidCompactPoseBoneIndex(LowerIndex) ||
		!IsValidCompactPoseBoneIndex(HandIndex))
	{
		return;
	}

	const FCompactPoseBoneIndex UpperParentIndex = BoneContainer.GetParentBoneIndex(UpperIndex);

	const FTransform UpperParentTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, UpperParentIndex);
	const FTransform OriginalUpperTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, UpperIndex);
	const FTransform OriginalLowerTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, LowerIndex);
	const FTransform OriginalHandTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, HandIndex);
	const bool bApplyAxisCalibration = bUseBindings && Bindings.bApplyAxisCalibration;
	const FRotator UpperRotation = ResolveCalibratedRotation(
		Snapshot.LeftUpperArmAdditiveRotation, Bindings.LeftUpperArmAxis, bApplyAxisCalibration);
	const FRotator LowerRotation = ResolveCalibratedRotation(
		Snapshot.LeftLowerArmAdditiveRotation, Bindings.LeftLowerArmAxis, bApplyAxisCalibration);
	const FRotator HandRotation = ResolveCalibratedRotation(
		Snapshot.LeftHandAdditiveRotation, Bindings.LeftHandAxis, bApplyAxisCalibration);
	if (Snapshot.bLeftArmFKMirroredSource)
	{
		const FCompactPoseBoneIndex RightUpperIndex = ResolveBoneIndex(
			BoneContainer, RightUpperArmBone,
			GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightUpperArm), TEXT("upperarm_r"));
		const FCompactPoseBoneIndex RightLowerIndex = ResolveBoneIndex(
			BoneContainer, RightLowerArmBone,
			GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightLowerArm), TEXT("lowerarm_r"));
		const FCompactPoseBoneIndex RightHandIndex = ResolveBoneIndex(
			BoneContainer, RightHandBone,
			GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightHand), TEXT("hand_r"));
		if (!IsValidCompactPoseBoneIndex(RightUpperIndex) ||
			!IsValidCompactPoseBoneIndex(RightLowerIndex) ||
			!IsValidCompactPoseBoneIndex(RightHandIndex))
		{
			return;
		}
		const FCompactPoseBoneIndex RightParentIndex = BoneContainer.GetParentBoneIndex(RightUpperIndex);
		const FLLMNPCArmChainTransforms Mirrored = FLLMNPCMotionMirror::MirrorRightArmFKAcrossSkeletonX(
			GetCurrentBoneTransformCS(Output, OutBoneTransforms, RightParentIndex),
			{
				GetCurrentBoneTransformCS(Output, OutBoneTransforms, RightUpperIndex),
				GetCurrentBoneTransformCS(Output, OutBoneTransforms, RightLowerIndex),
				GetCurrentBoneTransformCS(Output, OutBoneTransforms, RightHandIndex)
			},
			UpperRotation,
			LowerRotation,
			HandRotation,
			UpperParentTM,
			{ OriginalUpperTM, OriginalLowerTM, OriginalHandTM },
			InAlpha
		);
		AddOrReplaceBoneTransform(OutBoneTransforms, UpperIndex, Mirrored.UpperCS);
		AddOrReplaceBoneTransform(OutBoneTransforms, LowerIndex, Mirrored.LowerCS);
		AddOrReplaceBoneTransform(OutBoneTransforms, HandIndex, Mirrored.HandCS);
		return;
	}

	FTransform UpperLocalTM = OriginalUpperTM.GetRelativeTransform(UpperParentTM);
	FTransform LowerLocalTM = OriginalLowerTM.GetRelativeTransform(OriginalUpperTM);
	FTransform HandLocalTM = OriginalHandTM.GetRelativeTransform(OriginalLowerTM);

	UpperLocalTM = ApplyLocalRotationDelta(UpperLocalTM, UpperRotation, InAlpha);
	LowerLocalTM = ApplyLocalRotationDelta(LowerLocalTM, LowerRotation, InAlpha);
	HandLocalTM = ApplyLocalRotationDelta(HandLocalTM, HandRotation, InAlpha);

	FTransform UpperTM = UpperLocalTM * UpperParentTM;
	UpperTM.NormalizeRotation();
	FTransform LowerTM = LowerLocalTM * UpperTM;
	LowerTM.NormalizeRotation();
	FTransform HandTM = HandLocalTM * LowerTM;
	HandTM.NormalizeRotation();

	AddOrReplaceBoneTransform(OutBoneTransforms, UpperIndex, UpperTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, LowerIndex, LowerTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, HandIndex, HandTM);
}

void FAnimNode_LLMProceduralPose::PropagateRightHandChildrenCS(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms
) const
{
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	const FCompactPoseBoneIndex HandIndex = ResolveBoneIndex(
		BoneContainer,
		RightHandBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.RightHand),
		TEXT("hand_r")
	);
	if (!IsValidCompactPoseBoneIndex(HandIndex) || !HasBoneTransform(OutBoneTransforms, HandIndex))
	{
		return;
	}

	RebaseDescendantsToCurrentHierarchyCS(Output, OutBoneTransforms, HandIndex);
}

void FAnimNode_LLMProceduralPose::PropagateLeftHandChildrenCS(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms
) const
{
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	const FCompactPoseBoneIndex HandIndex = ResolveBoneIndex(
		BoneContainer,
		LeftHandBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.LeftHand),
		TEXT("hand_l")
	);
	if (!IsValidCompactPoseBoneIndex(HandIndex) || !HasBoneTransform(OutBoneTransforms, HandIndex))
	{
		return;
	}

	RebaseDescendantsToCurrentHierarchyCS(Output, OutBoneTransforms, HandIndex);
}

bool FAnimNode_LLMProceduralPose::HasAnyRightFingerBone(const FBoneContainer& RequiredBones) const
{
	struct FFingerBoneBinding
	{
		const FBoneReference& Bone;
		FName FallbackBoneName;
	};

	const FFingerBoneBinding FingerBones[] = {
		{RightThumb01Bone, TEXT("thumb_01_r")},
		{RightThumb02Bone, TEXT("thumb_02_r")},
		{RightThumb03Bone, TEXT("thumb_03_r")},
		{RightIndex01Bone, TEXT("index_01_r")},
		{RightIndex02Bone, TEXT("index_02_r")},
		{RightIndex03Bone, TEXT("index_03_r")},
		{RightMiddle01Bone, TEXT("middle_01_r")},
		{RightMiddle02Bone, TEXT("middle_02_r")},
		{RightMiddle03Bone, TEXT("middle_03_r")},
		{RightRing01Bone, TEXT("ring_01_r")},
		{RightRing02Bone, TEXT("ring_02_r")},
		{RightRing03Bone, TEXT("ring_03_r")},
		{RightPinky01Bone, TEXT("pinky_01_r")},
		{RightPinky02Bone, TEXT("pinky_02_r")},
		{RightPinky03Bone, TEXT("pinky_03_r")}
	};

	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	int32 BindingIndex = 0;
	for (const FFingerBoneBinding& FingerBone : FingerBones)
	{
		if (IsValidCompactPoseBoneIndex(ResolveBoneIndex(
			RequiredBones,
			FingerBone.Bone,
			GetSnapshotFingerBinding(bUseBindings, Bindings, Bindings.LeftFingerBones, BindingIndex),
			FingerBone.FallbackBoneName)))
		{
			return true;
		}
		++BindingIndex;
	}

	return false;
}

bool FAnimNode_LLMProceduralPose::HasAnyLeftFingerBone(const FBoneContainer& RequiredBones) const
{
	struct FFingerBoneBinding
	{
		const FBoneReference& Bone;
		FName FallbackBoneName;
	};
	const FFingerBoneBinding FingerBones[] = {
		{LeftThumb01Bone, TEXT("thumb_01_l")}, {LeftThumb02Bone, TEXT("thumb_02_l")}, {LeftThumb03Bone, TEXT("thumb_03_l")},
		{LeftIndex01Bone, TEXT("index_01_l")}, {LeftIndex02Bone, TEXT("index_02_l")}, {LeftIndex03Bone, TEXT("index_03_l")},
		{LeftMiddle01Bone, TEXT("middle_01_l")}, {LeftMiddle02Bone, TEXT("middle_02_l")}, {LeftMiddle03Bone, TEXT("middle_03_l")},
		{LeftRing01Bone, TEXT("ring_01_l")}, {LeftRing02Bone, TEXT("ring_02_l")}, {LeftRing03Bone, TEXT("ring_03_l")},
		{LeftPinky01Bone, TEXT("pinky_01_l")}, {LeftPinky02Bone, TEXT("pinky_02_l")}, {LeftPinky03Bone, TEXT("pinky_03_l")}
	};
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	int32 BindingIndex = 0;
	for (const FFingerBoneBinding& FingerBone : FingerBones)
	{
		if (IsValidCompactPoseBoneIndex(ResolveBoneIndex(
			RequiredBones,
			FingerBone.Bone,
			GetSnapshotFingerBinding(bUseBindings, Bindings, Bindings.RightFingerBones, BindingIndex),
			FingerBone.FallbackBoneName)))
		{
			return true;
		}
		++BindingIndex;
	}
	return false;
}

void FAnimNode_LLMProceduralPose::ApplyFingerRotationLocal(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	const FBoneReference& Bone,
	FName BoundBoneName,
	FName FallbackBoneName,
	const FRotator& Rotation,
	float InAlpha
) const
{
	if (InAlpha <= KINDA_SMALL_NUMBER || Rotation.IsNearlyZero())
	{
		return;
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FCompactPoseBoneIndex BoneIndex = ResolveBoneIndex(
		BoneContainer,
		Bone,
		BoundBoneName,
		FallbackBoneName
	);
	if (!IsValidCompactPoseBoneIndex(BoneIndex))
	{
		return;
	}

	const FCompactPoseBoneIndex ParentIndex = BoneContainer.GetParentBoneIndex(BoneIndex);
	if (!IsValidCompactPoseBoneIndex(ParentIndex))
	{
		return;
	}

	const FTransform ParentTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, ParentIndex);
	const FTransform OriginalParentTM = Output.Pose.GetComponentSpaceTransform(ParentIndex);
	const FTransform OriginalBoneTM = Output.Pose.GetComponentSpaceTransform(BoneIndex);

	FTransform LocalTM = OriginalBoneTM.GetRelativeTransform(OriginalParentTM);
	LocalTM = ApplyLocalRotationDelta(LocalTM, Rotation, InAlpha);

	FTransform NewBoneTM = LocalTM * ParentTM;
	NewBoneTM.NormalizeRotation();
	AddOrReplaceBoneTransform(OutBoneTransforms, BoneIndex, NewBoneTM);
}

void FAnimNode_LLMProceduralPose::ApplyRightFingerPoseLocal(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	float InAlpha
) const
{
	const float OpenAlpha = FMath::Clamp(Snapshot.RightFingersOpen * InAlpha, 0.0f, 1.0f);
	const float PointAlpha = FMath::Clamp(Snapshot.RightFingersPoint * InAlpha, 0.0f, 1.0f);
	if (OpenAlpha <= KINDA_SMALL_NUMBER && PointAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	struct FFingerPoseBinding
	{
		const FBoneReference* Bone;
		FName BoneName;
		FRotator OpenRotation;
		FRotator PointRotation;
	};
	const FFingerPoseBinding FingerBindings[] = {
		{&RightThumb01Bone, TEXT("thumb_01_r"), FRotator(-5.0f, 2.0f, 6.0f), FRotator(12.0f, -18.0f, -8.0f)},
		{&RightThumb02Bone, TEXT("thumb_02_r"), FRotator(-2.0f, 2.0f, 2.0f), FRotator(8.0f, -16.0f, -4.0f)},
		{&RightThumb03Bone, TEXT("thumb_03_r"), FRotator(0.0f, 1.0f, 0.0f), FRotator(2.0f, -8.0f, 0.0f)},
		{&RightIndex01Bone, TEXT("index_01_r"), FRotator(0.0f, 7.0f, 0.0f), FRotator(0.0f, -3.0f, 0.0f)},
		{&RightIndex02Bone, TEXT("index_02_r"), FRotator(0.0f, 5.0f, 0.0f), FRotator(0.0f, -2.0f, 0.0f)},
		{&RightIndex03Bone, TEXT("index_03_r"), FRotator(0.0f, 2.0f, 0.0f), FRotator(0.0f, -1.0f, 0.0f)},
		{&RightMiddle01Bone, TEXT("middle_01_r"), FRotator(0.0f, 6.0f, 0.0f), FRotator(0.0f, -44.0f, 0.0f)},
		{&RightMiddle02Bone, TEXT("middle_02_r"), FRotator(0.0f, 5.0f, 0.0f), FRotator(0.0f, -54.0f, 0.0f)},
		{&RightMiddle03Bone, TEXT("middle_03_r"), FRotator(0.0f, 2.0f, 0.0f), FRotator(0.0f, -28.0f, 0.0f)},
		{&RightRing01Bone, TEXT("ring_01_r"), FRotator(0.0f, 6.0f, 1.0f), FRotator(0.0f, -46.0f, 2.0f)},
		{&RightRing02Bone, TEXT("ring_02_r"), FRotator(0.0f, 5.0f, 0.0f), FRotator(0.0f, -56.0f, 0.0f)},
		{&RightRing03Bone, TEXT("ring_03_r"), FRotator(0.0f, 2.0f, 0.0f), FRotator(0.0f, -30.0f, 0.0f)},
		{&RightPinky01Bone, TEXT("pinky_01_r"), FRotator(0.0f, 6.0f, 2.0f), FRotator(2.0f, -48.0f, 4.0f)},
		{&RightPinky02Bone, TEXT("pinky_02_r"), FRotator(0.0f, 5.0f, 0.0f), FRotator(2.0f, -58.0f, 0.0f)},
		{&RightPinky03Bone, TEXT("pinky_03_r"), FRotator(0.0f, 2.0f, 0.0f), FRotator(0.0f, -32.0f, 0.0f)}
	};
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(FingerBindings); ++Index)
	{
		const FFingerPoseBinding& Binding = FingerBindings[Index];
		const FName BoundBoneName = GetSnapshotFingerBinding(
			bUseBindings, Bindings, Bindings.RightFingerBones, Index);
		const FRotator OpenRotation = Bindings.RightFingerOpenRotations.IsValidIndex(Index)
			? Bindings.RightFingerOpenRotations[Index]
			: Binding.OpenRotation;
		const FRotator PointRotation = Bindings.RightFingerPointRotations.IsValidIndex(Index)
			? Bindings.RightFingerPointRotations[Index]
			: Binding.PointRotation;
		if (OpenAlpha > KINDA_SMALL_NUMBER)
		{
			ApplyFingerRotationLocal(Output, OutBoneTransforms, *Binding.Bone, BoundBoneName, Binding.BoneName, OpenRotation, OpenAlpha);
		}
		if (PointAlpha > KINDA_SMALL_NUMBER)
		{
			ApplyFingerRotationLocal(Output, OutBoneTransforms, *Binding.Bone, BoundBoneName, Binding.BoneName, PointRotation, PointAlpha);
		}
	}
}

void FAnimNode_LLMProceduralPose::ApplyLeftFingerPoseLocal(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	float InAlpha
) const
{
	const float OpenAlpha = FMath::Clamp(Snapshot.LeftFingersOpen * InAlpha, 0.0f, 1.0f);
	const float PointAlpha = FMath::Clamp(Snapshot.LeftFingersPoint * InAlpha, 0.0f, 1.0f);
	if (OpenAlpha <= KINDA_SMALL_NUMBER && PointAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	struct FFingerPoseBinding
	{
		const FBoneReference* Bone;
		FName BoneName;
		FRotator OpenRotation;
		FRotator PointRotation;
	};
	const FFingerPoseBinding FingerBindings[] = {
		{&LeftThumb01Bone, TEXT("thumb_01_l"), FRotator(-5.0f, -2.0f, -6.0f), FRotator(12.0f, 18.0f, 8.0f)},
		{&LeftThumb02Bone, TEXT("thumb_02_l"), FRotator(-2.0f, -2.0f, -2.0f), FRotator(8.0f, 16.0f, 4.0f)},
		{&LeftThumb03Bone, TEXT("thumb_03_l"), FRotator(0.0f, -1.0f, 0.0f), FRotator(2.0f, 8.0f, 0.0f)},
		{&LeftIndex01Bone, TEXT("index_01_l"), FRotator(0.0f, -7.0f, 0.0f), FRotator(0.0f, 3.0f, 0.0f)},
		{&LeftIndex02Bone, TEXT("index_02_l"), FRotator(0.0f, -5.0f, 0.0f), FRotator(0.0f, 2.0f, 0.0f)},
		{&LeftIndex03Bone, TEXT("index_03_l"), FRotator(0.0f, -2.0f, 0.0f), FRotator(0.0f, 1.0f, 0.0f)},
		{&LeftMiddle01Bone, TEXT("middle_01_l"), FRotator(0.0f, -6.0f, 0.0f), FRotator(0.0f, 44.0f, 0.0f)},
		{&LeftMiddle02Bone, TEXT("middle_02_l"), FRotator(0.0f, -5.0f, 0.0f), FRotator(0.0f, 54.0f, 0.0f)},
		{&LeftMiddle03Bone, TEXT("middle_03_l"), FRotator(0.0f, -2.0f, 0.0f), FRotator(0.0f, 28.0f, 0.0f)},
		{&LeftRing01Bone, TEXT("ring_01_l"), FRotator(0.0f, -6.0f, -1.0f), FRotator(0.0f, 46.0f, -2.0f)},
		{&LeftRing02Bone, TEXT("ring_02_l"), FRotator(0.0f, -5.0f, 0.0f), FRotator(0.0f, 56.0f, 0.0f)},
		{&LeftRing03Bone, TEXT("ring_03_l"), FRotator(0.0f, -2.0f, 0.0f), FRotator(0.0f, 30.0f, 0.0f)},
		{&LeftPinky01Bone, TEXT("pinky_01_l"), FRotator(0.0f, -6.0f, -2.0f), FRotator(2.0f, 48.0f, -4.0f)},
		{&LeftPinky02Bone, TEXT("pinky_02_l"), FRotator(0.0f, -5.0f, 0.0f), FRotator(2.0f, 58.0f, 0.0f)},
		{&LeftPinky03Bone, TEXT("pinky_03_l"), FRotator(0.0f, -2.0f, 0.0f), FRotator(0.0f, 32.0f, 0.0f)}
	};
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(FingerBindings); ++Index)
	{
		const FFingerPoseBinding& Binding = FingerBindings[Index];
		const FName BoundBoneName = GetSnapshotFingerBinding(
			bUseBindings, Bindings, Bindings.LeftFingerBones, Index);
		const FRotator OpenRotation = Bindings.LeftFingerOpenRotations.IsValidIndex(Index)
			? Bindings.LeftFingerOpenRotations[Index]
			: Binding.OpenRotation;
		const FRotator PointRotation = Bindings.LeftFingerPointRotations.IsValidIndex(Index)
			? Bindings.LeftFingerPointRotations[Index]
			: Binding.PointRotation;
		if (OpenAlpha > KINDA_SMALL_NUMBER)
		{
			ApplyFingerRotationLocal(
				Output,
				OutBoneTransforms,
				*Binding.Bone,
				BoundBoneName,
				Binding.BoneName,
				OpenRotation,
				OpenAlpha
			);
		}
		if (PointAlpha > KINDA_SMALL_NUMBER)
		{
			ApplyFingerRotationLocal(
				Output,
				OutBoneTransforms,
				*Binding.Bone,
				BoundBoneName,
				Binding.BoneName,
				PointRotation,
				PointAlpha
			);
		}
	}
}

void FAnimNode_LLMProceduralPose::AddGazeToRotations(
	FComponentSpacePoseContext& Output,
	FRotator& InOutHeadRotation,
	FRotator& InOutChestRotation
) const
{
	const float GazeAlpha = FMath::Clamp(Snapshot.GazeAlpha * ActualAlpha, 0.0f, 1.0f);
	if (GazeAlpha <= KINDA_SMALL_NUMBER || Snapshot.GazeTargetCS.IsNearlyZero())
	{
		return;
	}

	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	const FLLMNPCPoseBoneBindings& Bindings = Snapshot.BoneBindings;
	const bool bUseBindings = bUseSnapshotBoneBindings && !Bindings.ProfileId.IsNone();
	const FCompactPoseBoneIndex HeadIndex = ResolveBoneIndex(
		BoneContainer,
		HeadBone,
		GetSnapshotBinding(bUseBindings, Bindings, Bindings.Head),
		TEXT("head")
	);
	if (!IsValidCompactPoseBoneIndex(HeadIndex))
	{
		return;
	}

	const FVector HeadLocation = Output.Pose.GetComponentSpaceTransform(HeadIndex).GetLocation();
	const FVector Direction = (Snapshot.GazeTargetCS - HeadLocation).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return;
	}

	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(Direction.Z, FMath::Max(1.0f, FVector2D(Direction.X, Direction.Y).Size())));

	InOutHeadRotation.Yaw += FMath::Clamp(Yaw, -35.0f, 35.0f) * GazeAlpha * 0.75f;
	InOutHeadRotation.Roll += FMath::Clamp(Pitch, -20.0f, 20.0f) * GazeAlpha * 0.35f;
	InOutChestRotation.Yaw += FMath::Clamp(Yaw, -20.0f, 20.0f) * GazeAlpha * 0.25f;
}
