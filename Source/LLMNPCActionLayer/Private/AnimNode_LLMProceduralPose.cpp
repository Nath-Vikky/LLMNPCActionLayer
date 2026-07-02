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

void RotateTransformAroundPivotCS(FTransform& Transform, const FVector& Pivot, const FQuat& DeltaQuat)
{
	Transform.SetLocation(Pivot + DeltaQuat.RotateVector(Transform.GetLocation() - Pivot));
	Transform.SetRotation(DeltaQuat * Transform.GetRotation());
	Transform.NormalizeRotation();
}

void RotateBoneAndChildrenCS(
	FTransform& BoneTM,
	TArray<FTransform*> ChildTransforms,
	const FQuat& DeltaQuat
)
{
	if (DeltaQuat.IsIdentity())
	{
		return;
	}

	const FVector Pivot = BoneTM.GetLocation();
	BoneTM.SetRotation(DeltaQuat * BoneTM.GetRotation());
	BoneTM.NormalizeRotation();

	for (FTransform* ChildTM : ChildTransforms)
	{
		if (ChildTM)
		{
			RotateTransformAroundPivotCS(*ChildTM, Pivot, DeltaQuat);
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
}

bool FAnimNode_LLMProceduralPose::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return HeadBone.IsValidToEvaluate(RequiredBones) ||
		ChestBone.IsValidToEvaluate(RequiredBones) ||
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

	ApplyRightArmAdditiveRotationsCS(Output, OutBoneTransforms, NodeAlpha);

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

void FAnimNode_LLMProceduralPose::ApplyRightArmAdditiveRotationsCS(
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

	FTransform UpperTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, UpperIndex);
	FTransform LowerTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, LowerIndex);
	FTransform HandTM = GetCurrentBoneTransformCS(Output, OutBoneTransforms, HandIndex);

	if (!Snapshot.RightUpperArmAdditiveRotation.IsNearlyZero())
	{
		const FQuat DeltaQuat = FQuat(Snapshot.RightUpperArmAdditiveRotation * InAlpha);
		RotateBoneAndChildrenCS(UpperTM, { &LowerTM, &HandTM }, DeltaQuat);
	}

	if (!Snapshot.RightLowerArmAdditiveRotation.IsNearlyZero())
	{
		const FQuat DeltaQuat = FQuat(Snapshot.RightLowerArmAdditiveRotation * InAlpha);
		RotateBoneAndChildrenCS(LowerTM, { &HandTM }, DeltaQuat);
	}

	if (!Snapshot.RightHandAdditiveRotation.IsNearlyZero())
	{
		const FQuat DeltaQuat = FQuat(Snapshot.RightHandAdditiveRotation * InAlpha);
		HandTM.SetRotation(DeltaQuat * HandTM.GetRotation());
		HandTM.NormalizeRotation();
	}

	AddOrReplaceBoneTransform(OutBoneTransforms, UpperIndex, UpperTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, LowerIndex, LowerTM);
	AddOrReplaceBoneTransform(OutBoneTransforms, HandIndex, HandTM);
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
