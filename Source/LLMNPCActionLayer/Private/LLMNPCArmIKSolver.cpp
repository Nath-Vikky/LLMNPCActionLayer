#include "LLMNPCArmIKSolver.h"

namespace
{
FVector ProjectOntoBendPlane(const FVector& Direction, const FVector& LimbDirection)
{
	const FVector SafeDirection = Direction.GetSafeNormal();
	if (SafeDirection.IsNearlyZero() || LimbDirection.IsNearlyZero())
	{
		return SafeDirection;
	}

	return (
		SafeDirection -
		LimbDirection * FVector::DotProduct(SafeDirection, LimbDirection)
	).GetSafeNormal();
}

FVector ProjectOntoPalmPlane(const FVector& Direction, const FVector& PalmNormal)
{
	const FVector SafeNormal = PalmNormal.GetSafeNormal();
	return (
		Direction -
		SafeNormal * FVector::DotProduct(Direction, SafeNormal)
	).GetSafeNormal();
}
}

FVector FLLMNPCArmIKSolver::BuildStableJointTarget(
	const FVector& RootPosition,
	const FVector& CurrentJointPosition,
	const FVector& DesiredEndPosition,
	const FVector& ConfiguredPoleDirection,
	const FVector& FallbackPoleDirection,
	float ChainLength,
	float CurrentPoseWeight
)
{
	const FVector LimbDirection = (DesiredEndPosition - RootPosition).GetSafeNormal();
	FVector ProfileBendDirection = ProjectOntoBendPlane(ConfiguredPoleDirection, LimbDirection);
	if (ProfileBendDirection.IsNearlyZero())
	{
		ProfileBendDirection = ProjectOntoBendPlane(FallbackPoleDirection, LimbDirection);
	}

	const FVector CurrentBendDirection = ProjectOntoBendPlane(
		CurrentJointPosition - RootPosition,
		LimbDirection
	);
	FVector StableBendDirection = ProfileBendDirection;
	if (StableBendDirection.IsNearlyZero())
	{
		StableBendDirection = CurrentBendDirection;
	}
	else if (
		!CurrentBendDirection.IsNearlyZero() &&
		FVector::DotProduct(StableBendDirection, CurrentBendDirection) > 0.0f)
	{
		const float PoseWeight = FMath::Clamp(CurrentPoseWeight, 0.0f, 1.0f);
		StableBendDirection = (
			StableBendDirection * (1.0f - PoseWeight) +
			CurrentBendDirection * PoseWeight
		).GetSafeNormal();
	}

	if (StableBendDirection.IsNearlyZero())
	{
		const FVector ReferenceAxis =
			FMath::Abs(FVector::DotProduct(LimbDirection, FVector::UpVector)) < 0.95f
				? FVector::UpVector
				: FVector::RightVector;
		StableBendDirection = FVector::CrossProduct(LimbDirection, ReferenceAxis).GetSafeNormal();
	}

	return RootPosition + StableBendDirection * FMath::Max(ChainLength, 1.0f);
}

void FLLMNPCArmIKSolver::MaintainEndEffectorRelativeRotation(
	const FTransform& OriginalParentTransform,
	const FTransform& OriginalEndTransform,
	const FTransform& SolvedParentTransform,
	FTransform& InOutSolvedEndTransform
)
{
	const FVector SolvedEndPosition = InOutSolvedEndTransform.GetLocation();
	const FTransform OriginalEndLocal = OriginalEndTransform.GetRelativeTransform(OriginalParentTransform);
	const FTransform EndFollowingSolvedParent = OriginalEndLocal * SolvedParentTransform;

	InOutSolvedEndTransform.SetRotation(EndFollowingSolvedParent.GetRotation());
	InOutSolvedEndTransform.SetScale3D(EndFollowingSolvedParent.GetScale3D());
	InOutSolvedEndTransform.SetLocation(SolvedEndPosition);
	InOutSolvedEndTransform.NormalizeRotation();
}

FQuat FLLMNPCArmIKSolver::BuildPalmFacingRotation(
	const FQuat& CurrentHandRotation,
	const FVector& CurrentFingerDirection,
	const FVector& CurrentPalmNormal,
	const FVector& DesiredFingerDirection,
	const FVector& DesiredPalmNormal
)
{
	const FVector CurrentPalm = CurrentPalmNormal.GetSafeNormal();
	const FVector TargetPalm = DesiredPalmNormal.GetSafeNormal();
	const FVector CurrentFinger = ProjectOntoPalmPlane(CurrentFingerDirection, CurrentPalm);
	const FVector TargetFinger = ProjectOntoPalmPlane(DesiredFingerDirection, TargetPalm);
	if (
		CurrentPalm.IsNearlyZero() ||
		TargetPalm.IsNearlyZero() ||
		CurrentFinger.IsNearlyZero() ||
		TargetFinger.IsNearlyZero()
	)
	{
		return CurrentHandRotation;
	}

	const FQuat PalmAlignment = FQuat::FindBetweenNormals(CurrentPalm, TargetPalm);
	const FVector PalmAlignedFinger = ProjectOntoPalmPlane(
		PalmAlignment.RotateVector(CurrentFinger),
		TargetPalm
	);
	if (PalmAlignedFinger.IsNearlyZero())
	{
		return (PalmAlignment * CurrentHandRotation).GetNormalized();
	}

	const float TwistAngle = FMath::Atan2(
		FVector::DotProduct(
			TargetPalm,
			FVector::CrossProduct(PalmAlignedFinger, TargetFinger)
		),
		FVector::DotProduct(PalmAlignedFinger, TargetFinger)
	);
	const FQuat FingerAlignment(TargetPalm, TwistAngle);
	return (FingerAlignment * PalmAlignment * CurrentHandRotation).GetNormalized();
}
