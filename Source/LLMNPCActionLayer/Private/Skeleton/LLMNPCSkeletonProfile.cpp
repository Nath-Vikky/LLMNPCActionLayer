#include "Skeleton/LLMNPCSkeletonProfile.h"

#include "Animation/Skeleton.h"
#include "Misc/SecureHash.h"
#include "ReferenceSkeleton.h"

namespace
{
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
}

FName ULLMNPCSkeletonProfile::FindBoneName(FName SemanticBone) const
{
	const FName* BoneName = SemanticBoneMap.Find(SemanticBone);
	return BoneName ? *BoneName : NAME_None;
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

	const FReferenceSkeleton& ReferenceSkeleton = LoadedSkeleton->GetReferenceSkeleton();
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
