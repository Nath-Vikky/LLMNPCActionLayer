#include "AnimNode_LLMProceduralPose.h"

#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
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
	FName FallbackBoneName
)
{
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

FCompactPoseBoneIndex ResolveBoneNameIndex(const FBoneContainer& BoneContainer, FName BoneName)
{
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
}

bool FAnimNode_LLMProceduralPose::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return HeadBone.IsValidToEvaluate(RequiredBones) ||
		ChestBone.IsValidToEvaluate(RequiredBones) ||
		HasAnyRightFingerBone(RequiredBones) ||
		(
			bEnableRightArmIK &&
			RightUpperArmBone.IsValidToEvaluate(RequiredBones) &&
			RightLowerArmBone.IsValidToEvaluate(RequiredBones) &&
			RightHandBone.IsValidToEvaluate(RequiredBones)
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

	// Manny's observed nod axis in the current project maps to component-space Z offset.
	FRotator HeadRotation(0.0f, Snapshot.HeadYaw, Snapshot.HeadPitch);
	FRotator ChestRotation(Snapshot.ChestPitch, Snapshot.ChestYaw, Snapshot.ChestRoll);
	AddGazeToRotations(Output, HeadRotation, ChestRotation);

	ApplyAdditiveRotationCS(Output, OutBoneTransforms, HeadBone, HeadRotation, NodeAlpha);
	ApplyAdditiveRotationCS(Output, OutBoneTransforms, ChestBone, ChestRotation, NodeAlpha);

	if (bEnableRightArmIK && Snapshot.RightHandIKAlpha > KINDA_SMALL_NUMBER)
	{
		ApplySimpleRightArmIK(Output, OutBoneTransforms);
	}

	ApplyRightArmAdditiveRotationsLocal(Output, OutBoneTransforms, NodeAlpha);
	PropagateRightHandChildrenCS(Output, OutBoneTransforms);
	ApplyRightFingerPoseLocal(Output, OutBoneTransforms, NodeAlpha);

	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}

void FAnimNode_LLMProceduralPose::ApplyAdditiveRotationCS(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	const FBoneReference& Bone,
	const FRotator& Rotation,
	float InAlpha
) const
{
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	if (!Bone.IsValidToEvaluate(BoneContainer))
	{
		return;
	}

	const FCompactPoseBoneIndex BoneIndex = Bone.GetCompactPoseIndex(BoneContainer);
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
	if (!RightUpperArmBone.IsValidToEvaluate(BoneContainer) ||
		!RightLowerArmBone.IsValidToEvaluate(BoneContainer) ||
		!RightHandBone.IsValidToEvaluate(BoneContainer))
	{
		return;
	}

	const float IKAlpha = FMath::Clamp(Snapshot.RightHandIKAlpha * ActualAlpha, 0.0f, 1.0f);
	if (IKAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FCompactPoseBoneIndex UpperIndex = RightUpperArmBone.GetCompactPoseIndex(BoneContainer);
	const FCompactPoseBoneIndex LowerIndex = RightLowerArmBone.GetCompactPoseIndex(BoneContainer);
	const FCompactPoseBoneIndex HandIndex = RightHandBone.GetCompactPoseIndex(BoneContainer);

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
	if (!RightUpperArmBone.IsValidToEvaluate(BoneContainer) ||
		!RightLowerArmBone.IsValidToEvaluate(BoneContainer) ||
		!RightHandBone.IsValidToEvaluate(BoneContainer))
	{
		return;
	}

	const FCompactPoseBoneIndex UpperIndex = RightUpperArmBone.GetCompactPoseIndex(BoneContainer);
	const FCompactPoseBoneIndex LowerIndex = RightLowerArmBone.GetCompactPoseIndex(BoneContainer);
	const FCompactPoseBoneIndex HandIndex = RightHandBone.GetCompactPoseIndex(BoneContainer);
	const FCompactPoseBoneIndex UpperParentIndex = BoneContainer.GetParentBoneIndex(UpperIndex);

	const FTransform UpperParentTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, UpperParentIndex);
	const FTransform OriginalUpperTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, UpperIndex);
	const FTransform OriginalLowerTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, LowerIndex);
	const FTransform OriginalHandTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, HandIndex);

	FTransform UpperLocalTM = OriginalUpperTM.GetRelativeTransform(UpperParentTM);
	FTransform LowerLocalTM = OriginalLowerTM.GetRelativeTransform(OriginalUpperTM);
	FTransform HandLocalTM = OriginalHandTM.GetRelativeTransform(OriginalLowerTM);

	UpperLocalTM = ApplyLocalRotationDelta(UpperLocalTM, Snapshot.RightUpperArmAdditiveRotation, InAlpha);
	LowerLocalTM = ApplyLocalRotationDelta(LowerLocalTM, Snapshot.RightLowerArmAdditiveRotation, InAlpha);
	HandLocalTM = ApplyLocalRotationDelta(HandLocalTM, Snapshot.RightHandAdditiveRotation, InAlpha);

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
	const FCompactPoseBoneIndex HandIndex = ResolveBoneIndex(BoneContainer, RightHandBone, TEXT("hand_r"));
	if (!IsValidCompactPoseBoneIndex(HandIndex) || !HasBoneTransform(OutBoneTransforms, HandIndex))
	{
		return;
	}

	const FName RightHandChildren[] = {
		TEXT("wrist_inner_r"),
		TEXT("wrist_outer_r"),
		TEXT("weapon_r"),

		TEXT("thumb_01_r"),
		TEXT("thumb_02_r"),
		TEXT("thumb_03_r"),

		TEXT("index_metacarpal_r"),
		TEXT("index_01_r"),
		TEXT("index_02_r"),
		TEXT("index_03_r"),

		TEXT("middle_metacarpal_r"),
		TEXT("middle_01_r"),
		TEXT("middle_02_r"),
		TEXT("middle_03_r"),

		TEXT("ring_metacarpal_r"),
		TEXT("ring_01_r"),
		TEXT("ring_02_r"),
		TEXT("ring_03_r"),

		TEXT("pinky_metacarpal_r"),
		TEXT("pinky_01_r"),
		TEXT("pinky_02_r"),
		TEXT("pinky_03_r")
	};

	for (const FName BoneName : RightHandChildren)
	{
		RebaseBoneToCurrentParentCS(Output, OutBoneTransforms, ResolveBoneNameIndex(BoneContainer, BoneName));
	}
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

	for (const FFingerBoneBinding& FingerBone : FingerBones)
	{
		if (IsValidCompactPoseBoneIndex(ResolveBoneIndex(RequiredBones, FingerBone.Bone, FingerBone.FallbackBoneName)))
		{
			return true;
		}
	}

	return false;
}

void FAnimNode_LLMProceduralPose::ApplyFingerRotationLocal(
	FComponentSpacePoseContext& Output,
	TArray<FBoneTransform>& OutBoneTransforms,
	const FBoneReference& Bone,
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
	const FCompactPoseBoneIndex BoneIndex = ResolveBoneIndex(BoneContainer, Bone, FallbackBoneName);
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

	if (OpenAlpha > KINDA_SMALL_NUMBER)
	{
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightThumb01Bone, TEXT("thumb_01_r"), FRotator(-5.0f, 2.0f, 6.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightThumb02Bone, TEXT("thumb_02_r"), FRotator(-2.0f, 2.0f, 2.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightThumb03Bone, TEXT("thumb_03_r"), FRotator(0.0f, 1.0f, 0.0f), OpenAlpha);

		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightIndex01Bone, TEXT("index_01_r"), FRotator(0.0f, 7.0f, 0.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightIndex02Bone, TEXT("index_02_r"), FRotator(0.0f, 5.0f, 0.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightIndex03Bone, TEXT("index_03_r"), FRotator(0.0f, 2.0f, 0.0f), OpenAlpha);

		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightMiddle01Bone, TEXT("middle_01_r"), FRotator(0.0f, 6.0f, 0.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightMiddle02Bone, TEXT("middle_02_r"), FRotator(0.0f, 5.0f, 0.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightMiddle03Bone, TEXT("middle_03_r"), FRotator(0.0f, 2.0f, 0.0f), OpenAlpha);

		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightRing01Bone, TEXT("ring_01_r"), FRotator(0.0f, 6.0f, 1.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightRing02Bone, TEXT("ring_02_r"), FRotator(0.0f, 5.0f, 0.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightRing03Bone, TEXT("ring_03_r"), FRotator(0.0f, 2.0f, 0.0f), OpenAlpha);

		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightPinky01Bone, TEXT("pinky_01_r"), FRotator(0.0f, 6.0f, 2.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightPinky02Bone, TEXT("pinky_02_r"), FRotator(0.0f, 5.0f, 0.0f), OpenAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightPinky03Bone, TEXT("pinky_03_r"), FRotator(0.0f, 2.0f, 0.0f), OpenAlpha);
	}

	if (PointAlpha > KINDA_SMALL_NUMBER)
	{
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightThumb01Bone, TEXT("thumb_01_r"), FRotator(12.0f, -18.0f, -8.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightThumb02Bone, TEXT("thumb_02_r"), FRotator(8.0f, -16.0f, -4.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightThumb03Bone, TEXT("thumb_03_r"), FRotator(2.0f, -8.0f, 0.0f), PointAlpha);

		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightIndex01Bone, TEXT("index_01_r"), FRotator(0.0f, -3.0f, 0.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightIndex02Bone, TEXT("index_02_r"), FRotator(0.0f, -2.0f, 0.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightIndex03Bone, TEXT("index_03_r"), FRotator(0.0f, -1.0f, 0.0f), PointAlpha);

		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightMiddle01Bone, TEXT("middle_01_r"), FRotator(0.0f, -44.0f, 0.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightMiddle02Bone, TEXT("middle_02_r"), FRotator(0.0f, -54.0f, 0.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightMiddle03Bone, TEXT("middle_03_r"), FRotator(0.0f, -28.0f, 0.0f), PointAlpha);

		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightRing01Bone, TEXT("ring_01_r"), FRotator(0.0f, -46.0f, 2.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightRing02Bone, TEXT("ring_02_r"), FRotator(0.0f, -56.0f, 0.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightRing03Bone, TEXT("ring_03_r"), FRotator(0.0f, -30.0f, 0.0f), PointAlpha);

		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightPinky01Bone, TEXT("pinky_01_r"), FRotator(2.0f, -48.0f, 4.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightPinky02Bone, TEXT("pinky_02_r"), FRotator(2.0f, -58.0f, 0.0f), PointAlpha);
		ApplyFingerRotationLocal(Output, OutBoneTransforms, RightPinky03Bone, TEXT("pinky_03_r"), FRotator(0.0f, -32.0f, 0.0f), PointAlpha);
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
	if (!HeadBone.IsValidToEvaluate(BoneContainer))
	{
		return;
	}

	const FCompactPoseBoneIndex HeadIndex = HeadBone.GetCompactPoseIndex(BoneContainer);
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
