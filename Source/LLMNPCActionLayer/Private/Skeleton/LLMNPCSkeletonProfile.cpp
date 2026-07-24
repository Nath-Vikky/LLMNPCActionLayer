#include "Skeleton/LLMNPCSkeletonProfile.h"

#include "Animation/Skeleton.h"
#include "Misc/SecureHash.h"
#include "ReferenceSkeleton.h"

namespace
{
const FName RequiredCoreSemanticBones[] = {
	TEXT("head"), TEXT("chest"),
	TEXT("upperarm_right"), TEXT("lowerarm_right"), TEXT("hand_right"),
	TEXT("upperarm_left"), TEXT("lowerarm_left"), TEXT("hand_left")
};

const TCHAR* FingerSemanticStems[] = {
	TEXT("thumb_01"), TEXT("thumb_02"), TEXT("thumb_03"),
	TEXT("index_01"), TEXT("index_02"), TEXT("index_03"),
	TEXT("middle_01"), TEXT("middle_02"), TEXT("middle_03"),
	TEXT("ring_01"), TEXT("ring_02"), TEXT("ring_03"),
	TEXT("pinky_01"), TEXT("pinky_02"), TEXT("pinky_03")
};

bool ResolveSemanticBone(
	const TMap<FName, FName>& SemanticBoneMap,
	FName SemanticBone,
	FName& OutBoneName
)
{
	const FName* BoneName = SemanticBoneMap.Find(SemanticBone);
	if (!BoneName || BoneName->IsNone())
	{
		return false;
	}

	OutBoneName = *BoneName;
	return true;
}

bool IsAxisBasisValid(const FLLMNPCBoneAxisBasis& Basis)
{
	const FVector Pitch = Basis.PitchAxis.GetSafeNormal();
	const FVector Yaw = Basis.YawAxis.GetSafeNormal();
	const FVector Roll = Basis.RollAxis.GetSafeNormal();
	return !Pitch.IsNearlyZero() && !Yaw.IsNearlyZero() && !Roll.IsNearlyZero() &&
		FMath::Abs(FVector::DotProduct(Pitch, Yaw)) < 0.05f &&
		FMath::Abs(FVector::DotProduct(Pitch, Roll)) < 0.05f &&
		FMath::Abs(FVector::DotProduct(Yaw, Roll)) < 0.05f;
}
}

FName ULLMNPCSkeletonProfile::FindBoneName(FName SemanticBone) const
{
	const FName* BoneName = SemanticBoneMap.Find(SemanticBone);
	return BoneName ? *BoneName : NAME_None;
}

FLLMNPCPoseBoneBindings ULLMNPCSkeletonProfile::BuildPoseBoneBindings() const
{
	FLLMNPCPoseBoneBindings Bindings;
	Bindings.ProfileId = ProfileId;
	Bindings.bApplyAxisCalibration = bApplyAxisCalibrationAtRuntime;
	const FVector ForwardDirection = ComponentForwardDirectionCS.GetSafeNormal();
	if (!ForwardDirection.IsNearlyZero())
	{
		Bindings.ComponentForwardDirectionCS = ForwardDirection;
	}
	const FVector UpDirection = (
		ComponentUpDirectionCS -
		Bindings.ComponentForwardDirectionCS *
			FVector::DotProduct(ComponentUpDirectionCS, Bindings.ComponentForwardDirectionCS)
	).GetSafeNormal();
	if (!UpDirection.IsNearlyZero())
	{
		Bindings.ComponentUpDirectionCS = UpDirection;
	}

	auto OverrideBone = [this](FName SemanticBone, FName& InOutBone)
	{
		const FName Resolved = FindBoneName(SemanticBone);
		if (!Resolved.IsNone())
		{
			InOutBone = Resolved;
		}
	};

	OverrideBone(TEXT("head"), Bindings.Head);
	OverrideBone(TEXT("chest"), Bindings.Chest);
	OverrideBone(TEXT("upperarm_right"), Bindings.RightUpperArm);
	OverrideBone(TEXT("lowerarm_right"), Bindings.RightLowerArm);
	OverrideBone(TEXT("hand_right"), Bindings.RightHand);
	OverrideBone(TEXT("upperarm_left"), Bindings.LeftUpperArm);
	OverrideBone(TEXT("lowerarm_left"), Bindings.LeftLowerArm);
	OverrideBone(TEXT("hand_left"), Bindings.LeftHand);

	auto OverrideIK = [this](
		FName ChainId,
		FVector& InOutPoleDirectionCS,
		float& InOutMaxReachScale)
	{
		const FLLMNPCIKChainProfile* Chain = IKChains.FindByPredicate(
			[ChainId](const FLLMNPCIKChainProfile& Candidate)
			{
				return Candidate.ChainId == ChainId;
			});
		if (!Chain)
		{
			return;
		}

		const FVector PoleDirection = Chain->PoleDirectionCS.GetSafeNormal();
		if (!PoleDirection.IsNearlyZero())
		{
			InOutPoleDirectionCS = PoleDirection;
		}
		if (FMath::IsFinite(Chain->MaxReachScale))
		{
			InOutMaxReachScale = FMath::Clamp(Chain->MaxReachScale, 0.01f, 1.0f);
		}
	};
	OverrideIK(
		TEXT("right_arm"),
		Bindings.RightArmIKPoleDirectionCS,
		Bindings.RightArmIKMaxReachScale);
	OverrideIK(
		TEXT("left_arm"),
		Bindings.LeftArmIKPoleDirectionCS,
		Bindings.LeftArmIKMaxReachScale);

	auto OverrideAxis = [this](FName SemanticBone, FLLMNPCResolvedAxisBasis& InOutBasis)
	{
		const FLLMNPCBoneAxisBasis* Basis = AxisBases.Find(SemanticBone);
		if (!Basis)
		{
			return;
		}
		InOutBasis.PitchAxis = Basis->PitchAxis;
		InOutBasis.YawAxis = Basis->YawAxis;
		InOutBasis.RollAxis = Basis->RollAxis;
		InOutBasis.MinAdditiveRotation = Basis->MinAdditiveRotation;
		InOutBasis.MaxAdditiveRotation = Basis->MaxAdditiveRotation;
	};
	OverrideAxis(TEXT("head"), Bindings.HeadAxis);
	OverrideAxis(TEXT("chest"), Bindings.ChestAxis);
	OverrideAxis(TEXT("upperarm_right"), Bindings.RightUpperArmAxis);
	OverrideAxis(TEXT("lowerarm_right"), Bindings.RightLowerArmAxis);
	OverrideAxis(TEXT("hand_right"), Bindings.RightHandAxis);
	OverrideAxis(TEXT("upperarm_left"), Bindings.LeftUpperArmAxis);
	OverrideAxis(TEXT("lowerarm_left"), Bindings.LeftLowerArmAxis);
	OverrideAxis(TEXT("hand_left"), Bindings.LeftHandAxis);

	const FLLMNPCFingerPoseProfile* OpenPose = FingerPoses.FindByPredicate(
		[](const FLLMNPCFingerPoseProfile& Pose)
		{
			return Pose.PoseId == TEXT("open") || Pose.PoseId == TEXT("fingers_open");
		});
	const FLLMNPCFingerPoseProfile* PointPose = FingerPoses.FindByPredicate(
		[](const FLLMNPCFingerPoseProfile& Pose)
		{
			return Pose.PoseId == TEXT("point") || Pose.PoseId == TEXT("fingers_point");
		});

	static const TCHAR* FingerNames[] = {
		TEXT("thumb_01"), TEXT("thumb_02"), TEXT("thumb_03"),
		TEXT("index_01"), TEXT("index_02"), TEXT("index_03"),
		TEXT("middle_01"), TEXT("middle_02"), TEXT("middle_03"),
		TEXT("ring_01"), TEXT("ring_02"), TEXT("ring_03"),
		TEXT("pinky_01"), TEXT("pinky_02"), TEXT("pinky_03")
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(FingerNames); ++Index)
	{
		OverrideBone(
			FName(*FString::Printf(TEXT("%s_right"), FingerNames[Index])),
			Bindings.RightFingerBones[Index]
		);
		OverrideBone(
			FName(*FString::Printf(TEXT("%s_left"), FingerNames[Index])),
			Bindings.LeftFingerBones[Index]
		);

		const FName RightSemantic(*FString::Printf(TEXT("%s_right"), FingerNames[Index]));
		const FName LeftSemantic(*FString::Printf(TEXT("%s_left"), FingerNames[Index]));
		if (OpenPose)
		{
			if (const FRotator* Rotation = OpenPose->SemanticBoneRotations.Find(RightSemantic))
			{
				Bindings.RightFingerOpenRotations[Index] = *Rotation;
			}
			if (const FRotator* Rotation = OpenPose->SemanticBoneRotations.Find(LeftSemantic))
			{
				Bindings.LeftFingerOpenRotations[Index] = *Rotation;
			}
		}
		if (PointPose)
		{
			if (const FRotator* Rotation = PointPose->SemanticBoneRotations.Find(RightSemantic))
			{
				Bindings.RightFingerPointRotations[Index] = *Rotation;
			}
			if (const FRotator* Rotation = PointPose->SemanticBoneRotations.Find(LeftSemantic))
			{
				Bindings.LeftFingerPointRotations[Index] = *Rotation;
			}
		}
	}

	return Bindings;
}

bool ULLMNPCSkeletonProfile::IsCompatibleSkeleton(const USkeleton* CandidateSkeleton) const
{
	if (!CandidateSkeleton)
	{
		return false;
	}

	if (SkeletonSignature.IsEmpty())
	{
		return Skeleton.Get() == CandidateSkeleton;
	}

	return SkeletonSignature == BuildSkeletonSignature(CandidateSkeleton, SemanticVersion);
}

bool ULLMNPCSkeletonProfile::ValidateProfile(FString& OutError) const
{
	OutError.Reset();
	if (ProfileId.IsNone())
	{
		OutError = TEXT("LLMNPC_SKELETON_PROFILE_ID_MISSING");
		return false;
	}

	const USkeleton* LoadedSkeleton = Skeleton.LoadSynchronous();
	if (!LoadedSkeleton)
	{
		OutError = TEXT("LLMNPC_SKELETON_PROFILE_ASSET_MISSING");
		return false;
	}
	const FVector ForwardDirection = ComponentForwardDirectionCS.GetSafeNormal();
	const FVector UpDirection = ComponentUpDirectionCS.GetSafeNormal();
	if (
		ForwardDirection.IsNearlyZero() ||
		UpDirection.IsNearlyZero() ||
		FMath::Abs(FVector::DotProduct(ForwardDirection, UpDirection)) > 0.05f
	)
	{
		OutError = TEXT("LLMNPC_SKELETON_PROFILE_COMPONENT_BASIS_INVALID");
		return false;
	}

	const FReferenceSkeleton& ReferenceSkeleton = LoadedSkeleton->GetReferenceSkeleton();
	for (const FName SemanticBone : RequiredCoreSemanticBones)
	{
		if (!SemanticBoneMap.Contains(SemanticBone))
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_SKELETON_PROFILE_REQUIRED_SEMANTIC_MISSING:%s"),
				*SemanticBone.ToString()
			);
			return false;
		}
	}
	for (const TPair<FName, FName>& Pair : SemanticBoneMap)
	{
		if (Pair.Key.IsNone() || Pair.Value.IsNone())
		{
			OutError = TEXT("LLMNPC_SKELETON_PROFILE_EMPTY_BONE_MAPPING");
			return false;
		}

		if (ReferenceSkeleton.FindBoneIndex(Pair.Value) == INDEX_NONE)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_SKELETON_PROFILE_BONE_NOT_FOUND:%s"),
				*Pair.Value.ToString()
			);
			return false;
		}
	}

	for (const TPair<FName, FLLMNPCBoneAxisBasis>& Pair : AxisBases)
	{
		if (!SemanticBoneMap.Contains(Pair.Key))
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_SKELETON_PROFILE_AXIS_SEMANTIC_NOT_FOUND:%s"),
				*Pair.Key.ToString()
			);
			return false;
		}
		if (!IsAxisBasisValid(Pair.Value))
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_SKELETON_PROFILE_AXIS_BASIS_INVALID:%s"),
				*Pair.Key.ToString()
			);
			return false;
		}
	}

	for (const FLLMNPCIKChainProfile& Chain : IKChains)
	{
		FName ResolvedBone;
		if (
			Chain.ChainId.IsNone() ||
			!ResolveSemanticBone(SemanticBoneMap, Chain.RootBoneSemantic, ResolvedBone) ||
			!ResolveSemanticBone(SemanticBoneMap, Chain.MidBoneSemantic, ResolvedBone) ||
			!ResolveSemanticBone(SemanticBoneMap, Chain.EndBoneSemantic, ResolvedBone)
		)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_SKELETON_PROFILE_INVALID_IK_CHAIN:%s"),
				*Chain.ChainId.ToString()
			);
			return false;
		}
	}

	for (const FLLMNPCFingerPoseProfile& Pose : FingerPoses)
	{
		if (Pose.PoseId.IsNone())
		{
			OutError = TEXT("LLMNPC_SKELETON_PROFILE_FINGER_POSE_ID_MISSING");
			return false;
		}

		for (const TPair<FName, FRotator>& Pair : Pose.SemanticBoneRotations)
		{
			if (!SemanticBoneMap.Contains(Pair.Key))
			{
				OutError = FString::Printf(
					TEXT("LLMNPC_SKELETON_PROFILE_FINGER_SEMANTIC_NOT_FOUND:%s"),
					*Pair.Key.ToString()
				);
				return false;
			}
		}
	}

	if (SkeletonSignature != BuildSkeletonSignature(LoadedSkeleton, SemanticVersion))
	{
		OutError = TEXT("LLMNPC_SKELETON_PROFILE_SIGNATURE_STALE");
		return false;
	}

	return true;
}

FLLMNPCSkeletonProfileQualityReport ULLMNPCSkeletonProfile::BuildQualityReport() const
{
	FLLMNPCSkeletonProfileQualityReport Report;
	Report.ProfileId = ProfileId;
	Report.SkeletonPath = Skeleton.ToSoftObjectPath().ToString();
	Report.SkeletonSignature = SkeletonSignature;
	Report.MappedBoneCount = SemanticBoneMap.Num();
	Report.IKChainCount = IKChains.Num();

	const USkeleton* LoadedSkeleton = Skeleton.LoadSynchronous();
	if (ProfileId.IsNone())
	{
		Report.Errors.Add(TEXT("LLMNPC_SKELETON_PROFILE_ID_MISSING"));
	}
	if (!LoadedSkeleton)
	{
		Report.Errors.Add(TEXT("LLMNPC_SKELETON_PROFILE_ASSET_MISSING"));
		return Report;
	}

	const FReferenceSkeleton& ReferenceSkeleton = LoadedSkeleton->GetReferenceSkeleton();
	int32 CoreBonesFound = 0;
	int32 CalibratedCoreBones = 0;
	for (const FName SemanticBone : RequiredCoreSemanticBones)
	{
		const FName BoneName = FindBoneName(SemanticBone);
		if (!BoneName.IsNone() && ReferenceSkeleton.FindBoneIndex(BoneName) != INDEX_NONE)
		{
			++CoreBonesFound;
		}
		else
		{
			Report.Errors.Add(FString::Printf(
				TEXT("LLMNPC_SKELETON_PROFILE_REQUIRED_SEMANTIC_MISSING:%s"),
				*SemanticBone.ToString()));
		}

		if (const FLLMNPCBoneAxisBasis* Basis = AxisBases.Find(SemanticBone))
		{
			if (IsAxisBasisValid(*Basis))
			{
				++CalibratedCoreBones;
			}
			else
			{
				Report.Errors.Add(FString::Printf(
					TEXT("LLMNPC_SKELETON_PROFILE_AXIS_BASIS_INVALID:%s"),
					*SemanticBone.ToString()));
			}
		}
	}
	Report.CoreBoneCoverage = static_cast<float>(CoreBonesFound) / UE_ARRAY_COUNT(RequiredCoreSemanticBones);
	Report.AxisCalibrationCoverage = static_cast<float>(CalibratedCoreBones) / UE_ARRAY_COUNT(RequiredCoreSemanticBones);

	int32 FingerBonesFound = 0;
	int32 FingerPoseRotationsFound = 0;
	const FLLMNPCFingerPoseProfile* OpenPose = FingerPoses.FindByPredicate(
		[](const FLLMNPCFingerPoseProfile& Pose)
		{
			return Pose.PoseId == TEXT("open") || Pose.PoseId == TEXT("fingers_open");
		});
	const FLLMNPCFingerPoseProfile* PointPose = FingerPoses.FindByPredicate(
		[](const FLLMNPCFingerPoseProfile& Pose)
		{
			return Pose.PoseId == TEXT("point") || Pose.PoseId == TEXT("fingers_point");
		});
	for (const TCHAR* Stem : FingerSemanticStems)
	{
		for (const TCHAR* Side : {TEXT("right"), TEXT("left")})
		{
			const FName SemanticBone(*FString::Printf(TEXT("%s_%s"), Stem, Side));
			const FName BoneName = FindBoneName(SemanticBone);
			if (!BoneName.IsNone() && ReferenceSkeleton.FindBoneIndex(BoneName) != INDEX_NONE)
			{
				++FingerBonesFound;
			}
			FingerPoseRotationsFound += OpenPose && OpenPose->SemanticBoneRotations.Contains(SemanticBone) ? 1 : 0;
			FingerPoseRotationsFound += PointPose && PointPose->SemanticBoneRotations.Contains(SemanticBone) ? 1 : 0;
		}
	}
	Report.FingerBoneCoverage = static_cast<float>(FingerBonesFound) / 30.0f;
	Report.FingerPoseCoverage = static_cast<float>(FingerPoseRotationsFound) / 60.0f;
	Report.bSignatureCurrent = SkeletonSignature == BuildSkeletonSignature(LoadedSkeleton, SemanticVersion);

	if (Report.AxisCalibrationCoverage < 1.0f)
	{
		Report.Warnings.Add(TEXT("LLMNPC_SKELETON_PROFILE_AXIS_CALIBRATION_INCOMPLETE"));
	}
	if (Report.FingerBoneCoverage < 1.0f)
	{
		Report.Warnings.Add(TEXT("LLMNPC_SKELETON_PROFILE_FINGER_MAPPING_INCOMPLETE"));
	}
	if (Report.FingerPoseCoverage < 1.0f)
	{
		Report.Warnings.Add(TEXT("LLMNPC_SKELETON_PROFILE_FINGER_CALIBRATION_INCOMPLETE"));
	}
	if (IKChains.IsEmpty())
	{
		Report.Warnings.Add(TEXT("LLMNPC_SKELETON_PROFILE_IK_CHAINS_MISSING"));
	}
	if (!Report.bSignatureCurrent)
	{
		Report.Errors.Add(TEXT("LLMNPC_SKELETON_PROFILE_SIGNATURE_STALE"));
	}

	Report.bPassed = Report.Errors.IsEmpty() && Report.CoreBoneCoverage >= 1.0f;
	return Report;
}

void ULLMNPCSkeletonProfile::RefreshSkeletonSignature()
{
	SkeletonSignature = BuildSkeletonSignature(Skeleton.LoadSynchronous(), SemanticVersion);
}

FPrimaryAssetId ULLMNPCSkeletonProfile::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(
		TEXT("LLMNPCSkeletonProfile"),
		ProfileId.IsNone() ? GetFName() : ProfileId
	);
}

FString ULLMNPCSkeletonProfile::BuildSkeletonSignature(
	const USkeleton* InSkeleton,
	const FString& ProfileVersion
)
{
	if (!InSkeleton)
	{
		return FString();
	}

	const FReferenceSkeleton& ReferenceSkeleton = InSkeleton->GetReferenceSkeleton();
	const TArray<FTransform>& ReferencePose = ReferenceSkeleton.GetRefBonePose();

	FString Canonical = FString::Printf(
		TEXT("%s|%d\n"),
		*ProfileVersion,
		ReferenceSkeleton.GetNum()
	);

	for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNum(); ++BoneIndex)
	{
		const FMeshBoneInfo& BoneInfo = ReferenceSkeleton.GetRefBoneInfo()[BoneIndex];
		const FTransform& Transform = ReferencePose[BoneIndex];
		const FVector Translation = Transform.GetTranslation();
		const FQuat Rotation = Transform.GetRotation();
		const FVector Scale = Transform.GetScale3D();

		Canonical += FString::Printf(
			TEXT("%d|%s|%d|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g\n"),
			BoneIndex,
			*BoneInfo.Name.ToString(),
			BoneInfo.ParentIndex,
			Translation.X,
			Translation.Y,
			Translation.Z,
			Rotation.X,
			Rotation.Y,
			Rotation.Z,
			Rotation.W,
			Scale.X,
			Scale.Y,
			Scale.Z
		);
	}

	return FString::Printf(TEXT("md5:%s"), *FMD5::HashAnsiString(*Canonical));
}
