#include "LLMNPCMotionMirror.h"

namespace
{
FQuat ScaledDelta(const FRotator& Rotation, float Alpha)
{
	const FQuat FullDelta(Rotation);
	return Alpha >= 1.0f - KINDA_SMALL_NUMBER
		? FullDelta
		: FQuat::Slerp(FQuat::Identity, FullDelta, FMath::Clamp(Alpha, 0.0f, 1.0f)).GetNormalized();
}

FTransform ApplyDelta(const FTransform& LocalTransform, const FRotator& Rotation, float Alpha)
{
	FTransform Result = LocalTransform;
	Result.SetRotation(ScaledDelta(Rotation, Alpha) * Result.GetRotation());
	Result.NormalizeRotation();
	return Result;
}

FTransform BuildLocalWithDesiredRotation(
	const FTransform& OriginalLocal,
	const FTransform& DesiredComponent,
	const FTransform& DesiredParent
)
{
	FTransform Result = DesiredComponent.GetRelativeTransform(DesiredParent);
	Result.SetLocation(OriginalLocal.GetLocation());
	Result.SetScale3D(OriginalLocal.GetScale3D());
	Result.NormalizeRotation();
	return Result;
}

FQuat MirroredComponentDelta(const FQuat& Original, const FQuat& Modified)
{
	const FQuat Delta = (Modified * Original.Inverse()).GetNormalized();
	return FLLMNPCMotionMirror::MirrorComponentRotationAcrossSkeletonX(Delta);
}
}

FQuat FLLMNPCMotionMirror::MirrorComponentRotationAcrossSkeletonX(const FQuat& Rotation)
{
	return FQuat(Rotation.X, -Rotation.Y, -Rotation.Z, Rotation.W).GetNormalized();
}

FLLMNPCArmChainTransforms FLLMNPCMotionMirror::MirrorRightArmFKAcrossSkeletonX(
	const FTransform& RightParentCS,
	const FLLMNPCArmChainTransforms& RightOriginal,
	const FRotator& RightUpperDelta,
	const FRotator& RightLowerDelta,
	const FRotator& RightHandDelta,
	const FTransform& LeftParentCS,
	const FLLMNPCArmChainTransforms& LeftOriginal,
	float Alpha
)
{
	FTransform RightUpperLocal = RightOriginal.UpperCS.GetRelativeTransform(RightParentCS);
	FTransform RightLowerLocal = RightOriginal.LowerCS.GetRelativeTransform(RightOriginal.UpperCS);
	FTransform RightHandLocal = RightOriginal.HandCS.GetRelativeTransform(RightOriginal.LowerCS);
	RightUpperLocal = ApplyDelta(RightUpperLocal, RightUpperDelta, Alpha);
	RightLowerLocal = ApplyDelta(RightLowerLocal, RightLowerDelta, Alpha);
	RightHandLocal = ApplyDelta(RightHandLocal, RightHandDelta, Alpha);

	FLLMNPCArmChainTransforms RightModified;
	RightModified.UpperCS = RightUpperLocal * RightParentCS;
	RightModified.LowerCS = RightLowerLocal * RightModified.UpperCS;
	RightModified.HandCS = RightHandLocal * RightModified.LowerCS;
	RightModified.UpperCS.NormalizeRotation();
	RightModified.LowerCS.NormalizeRotation();
	RightModified.HandCS.NormalizeRotation();

	FTransform DesiredUpperCS = LeftOriginal.UpperCS;
	DesiredUpperCS.SetRotation(
		MirroredComponentDelta(RightOriginal.UpperCS.GetRotation(), RightModified.UpperCS.GetRotation()) *
		LeftOriginal.UpperCS.GetRotation()
	);
	DesiredUpperCS.NormalizeRotation();

	FTransform DesiredLowerCS = LeftOriginal.LowerCS;
	DesiredLowerCS.SetRotation(
		MirroredComponentDelta(RightOriginal.LowerCS.GetRotation(), RightModified.LowerCS.GetRotation()) *
		LeftOriginal.LowerCS.GetRotation()
	);
	DesiredLowerCS.NormalizeRotation();

	FTransform DesiredHandCS = LeftOriginal.HandCS;
	DesiredHandCS.SetRotation(
		MirroredComponentDelta(RightOriginal.HandCS.GetRotation(), RightModified.HandCS.GetRotation()) *
		LeftOriginal.HandCS.GetRotation()
	);
	DesiredHandCS.NormalizeRotation();

	const FTransform LeftUpperLocal = LeftOriginal.UpperCS.GetRelativeTransform(LeftParentCS);
	const FTransform LeftLowerLocal = LeftOriginal.LowerCS.GetRelativeTransform(LeftOriginal.UpperCS);
	const FTransform LeftHandLocal = LeftOriginal.HandCS.GetRelativeTransform(LeftOriginal.LowerCS);

	FLLMNPCArmChainTransforms Result;
	Result.UpperCS = BuildLocalWithDesiredRotation(LeftUpperLocal, DesiredUpperCS, LeftParentCS) * LeftParentCS;
	Result.LowerCS = BuildLocalWithDesiredRotation(LeftLowerLocal, DesiredLowerCS, Result.UpperCS) * Result.UpperCS;
	Result.HandCS = BuildLocalWithDesiredRotation(LeftHandLocal, DesiredHandCS, Result.LowerCS) * Result.LowerCS;
	Result.UpperCS.NormalizeRotation();
	Result.LowerCS.NormalizeRotation();
	Result.HandCS.NormalizeRotation();
	return Result;
}
