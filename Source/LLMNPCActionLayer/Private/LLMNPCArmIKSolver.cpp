#include "LLMNPCArmIKSolver.h"

#include "TwoBoneIK.h"

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

FQuat NormalizeShortestArc(FQuat Rotation)
{
	Rotation.Normalize();
	if (Rotation.W < 0.0f)
	{
		Rotation.X *= -1.0f;
		Rotation.Y *= -1.0f;
		Rotation.Z *= -1.0f;
		Rotation.W *= -1.0f;
	}
	return Rotation;
}

void DecomposeSwingTwist(
	const FQuat& Rotation,
	const FVector& TwistAxis,
	FQuat& OutSwing,
	float& OutSignedTwistRadians
)
{
	const FVector SafeAxis = TwistAxis.GetSafeNormal();
	const FQuat SafeRotation = NormalizeShortestArc(Rotation);
	const FVector RotationVector(
		SafeRotation.X,
		SafeRotation.Y,
		SafeRotation.Z
	);
	const FVector ProjectedVector =
		SafeAxis * FVector::DotProduct(RotationVector, SafeAxis);
	FQuat Twist(
		ProjectedVector.X,
		ProjectedVector.Y,
		ProjectedVector.Z,
		SafeRotation.W
	);
	if (Twist.SizeSquared() <= UE_SMALL_NUMBER)
	{
		Twist = FQuat::Identity;
	}
	else
	{
		Twist = NormalizeShortestArc(Twist);
	}

	OutSignedTwistRadians = FMath::UnwindRadians(
		2.0f * FMath::Atan2(
			FVector::DotProduct(
				FVector(Twist.X, Twist.Y, Twist.Z),
				SafeAxis
			),
			Twist.W
		)
	);
	OutSwing = NormalizeShortestArc(SafeRotation * Twist.Inverse());
}

FQuat ClampSwing(const FQuat& Swing, float MaxSwingRadians)
{
	const FQuat SafeSwing = NormalizeShortestArc(Swing);
	const float SwingRadians = 2.0f * FMath::Acos(
		FMath::Clamp(SafeSwing.W, -1.0f, 1.0f)
	);
	if (
		SwingRadians <= MaxSwingRadians ||
		SwingRadians <= KINDA_SMALL_NUMBER
	)
	{
		return SafeSwing;
	}

	return FQuat::Slerp(
		FQuat::Identity,
		SafeSwing,
		MaxSwingRadians / SwingRadians
	).GetNormalized();
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

void FLLMNPCArmIKSolver::SolveAtBlendedEffector(
	FTransform& InOutUpperTransform,
	FTransform& InOutLowerTransform,
	FTransform& InOutHandTransform,
	const FVector& DesiredEndPosition,
	const FVector& ConfiguredPoleDirection,
	const FVector& FallbackPoleDirection,
	float Alpha,
	float CurrentPoseWeight
)
{
	const float SafeAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (SafeAlpha <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const FTransform OriginalLower = InOutLowerTransform;
	const FTransform OriginalHand = InOutHandTransform;
	const FVector RootPosition = InOutUpperTransform.GetLocation();
	const float ChainLength =
		(InOutLowerTransform.GetLocation() - RootPosition).Size() +
		(InOutHandTransform.GetLocation() -
			InOutLowerTransform.GetLocation()).Size();
	const FVector BlendedEndPosition = FMath::Lerp(
		OriginalHand.GetLocation(),
		DesiredEndPosition,
		SafeAlpha
	);
	const FVector JointTarget = BuildStableJointTarget(
		RootPosition,
		OriginalLower.GetLocation(),
		BlendedEndPosition,
		ConfiguredPoleDirection,
		FallbackPoleDirection,
		ChainLength,
		CurrentPoseWeight
	);

	AnimationCore::SolveTwoBoneIK(
		InOutUpperTransform,
		InOutLowerTransform,
		InOutHandTransform,
		JointTarget,
		BlendedEndPosition,
		false,
		1.0f,
		1.2f
	);
	MaintainEndEffectorRelativeRotation(
		OriginalLower,
		OriginalHand,
		InOutLowerTransform,
		InOutHandTransform
	);
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

bool FLLMNPCArmIKSolver::BuildStableContactPalmBasis(
	const FVector& ComponentForwardDirection,
	const FVector& ComponentUpDirection,
	const FVector& ContactDirection,
	bool bRightHand,
	FVector& OutFingerDirection,
	FVector& OutPalmNormal
)
{
	const FVector Forward = ComponentForwardDirection.GetSafeNormal();
	const FVector Up = (
		ComponentUpDirection -
		Forward * FVector::DotProduct(ComponentUpDirection, Forward)
	).GetSafeNormal();
	const FVector Left = FVector::CrossProduct(Forward, Up).GetSafeNormal();
	if (Forward.IsNearlyZero() || Up.IsNearlyZero() || Left.IsNearlyZero())
	{
		OutFingerDirection = FVector::ZeroVector;
		OutPalmNormal = FVector::ZeroVector;
		return false;
	}

	const float LateralContact = FVector::DotProduct(
		ContactDirection,
		Left
	);
	const float FacingSign = FMath::Abs(LateralContact) > KINDA_SMALL_NUMBER
		? FMath::Sign(LateralContact)
		: (bRightHand ? 1.0f : -1.0f);
	OutPalmNormal = Left * FacingSign;

	const FVector ReferenceFingerDirection = (
		Forward * 0.68f +
		Up * 0.73f
	).GetSafeNormal();
	OutFingerDirection = ProjectOntoPalmPlane(
		ReferenceFingerDirection,
		OutPalmNormal
	);
	return
		!OutFingerDirection.IsNearlyZero() &&
		!OutPalmNormal.IsNearlyZero();
}

float FLLMNPCArmIKSolver::ApplyConstrainedWristOrientation(
	const FTransform& LowerArmTransform,
	FTransform& InOutHandTransform,
	const FQuat& DesiredHandRotation,
	float Alpha,
	float MaxAxialTwistDegrees,
	float MaxWristSwingDegrees
)
{
	const float SafeAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (SafeAlpha <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const FVector ForearmAxis = (
		InOutHandTransform.GetLocation() -
		LowerArmTransform.GetLocation()
	).GetSafeNormal();
	if (ForearmAxis.IsNearlyZero())
	{
		return 0.0f;
	}

	const FQuat OriginalHandRotation =
		InOutHandTransform.GetRotation().GetNormalized();
	const FQuat BlendedTarget = FQuat::Slerp(
		OriginalHandRotation,
		DesiredHandRotation.GetNormalized(),
		SafeAlpha
	).GetNormalized();
	const FQuat DesiredDelta = NormalizeShortestArc(
		BlendedTarget * OriginalHandRotation.Inverse()
	);

	FQuat Swing;
	float DesiredTwistRadians = 0.0f;
	DecomposeSwingTwist(
		DesiredDelta,
		ForearmAxis,
		Swing,
		DesiredTwistRadians
	);

	const float MaxAxialTwistRadians = FMath::DegreesToRadians(
		FMath::Max(MaxAxialTwistDegrees, 0.0f)
	);
	const float MaxWristSwingRadians = FMath::DegreesToRadians(
		FMath::Max(MaxWristSwingDegrees, 0.0f)
	);
	const float AppliedTwistRadians = FMath::Clamp(
		DesiredTwistRadians,
		-MaxAxialTwistRadians,
		MaxAxialTwistRadians
	);

	const FQuat ClampedSwing = ClampSwing(
		Swing,
		MaxWristSwingRadians
	);
	const FQuat AppliedTwist(ForearmAxis, AppliedTwistRadians);
	InOutHandTransform.SetRotation(
		(ClampedSwing *
			AppliedTwist *
			OriginalHandRotation).GetNormalized()
	);
	InOutHandTransform.NormalizeRotation();
	return AppliedTwistRadians;
}
