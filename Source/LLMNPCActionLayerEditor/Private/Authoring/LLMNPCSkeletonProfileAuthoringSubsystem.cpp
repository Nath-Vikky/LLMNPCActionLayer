#include "Authoring/LLMNPCSkeletonProfileAuthoringSubsystem.h"

#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "LLMNPCControlManifest.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
struct FSemanticBoneCandidates
{
	FName Semantic;
	TArray<FName> Candidates;
};

const FSemanticBoneCandidates CoreBoneCandidates[] = {
	{TEXT("head"), {TEXT("head"), TEXT("Head"), TEXT("mixamorig:Head")}},
	{TEXT("chest"), {TEXT("spine_03"), TEXT("upperchest"), TEXT("UpperChest"), TEXT("Chest"), TEXT("mixamorig:Spine2")}},
	{TEXT("shoulder_right"), {TEXT("clavicle_r"), TEXT("RightShoulder"), TEXT("mixamorig:RightShoulder")}},
	{TEXT("upperarm_right"), {TEXT("upperarm_r"), TEXT("RightArm"), TEXT("mixamorig:RightArm")}},
	{TEXT("lowerarm_right"), {TEXT("lowerarm_r"), TEXT("RightForeArm"), TEXT("mixamorig:RightForeArm")}},
	{TEXT("hand_right"), {TEXT("hand_r"), TEXT("RightHand"), TEXT("mixamorig:RightHand")}},
	{TEXT("shoulder_left"), {TEXT("clavicle_l"), TEXT("LeftShoulder"), TEXT("mixamorig:LeftShoulder")}},
	{TEXT("upperarm_left"), {TEXT("upperarm_l"), TEXT("LeftArm"), TEXT("mixamorig:LeftArm")}},
	{TEXT("lowerarm_left"), {TEXT("lowerarm_l"), TEXT("LeftForeArm"), TEXT("mixamorig:LeftForeArm")}},
	{TEXT("hand_left"), {TEXT("hand_l"), TEXT("LeftHand"), TEXT("mixamorig:LeftHand")}}
};

const FSemanticBoneCandidates OptionalBoneCandidates[] = {
	{TEXT("pelvis"), {TEXT("pelvis"), TEXT("Hips"), TEXT("mixamorig:Hips")}},
	{TEXT("foot_right"), {TEXT("foot_r"), TEXT("RightFoot"), TEXT("mixamorig:RightFoot")}},
	{TEXT("foot_left"), {TEXT("foot_l"), TEXT("LeftFoot"), TEXT("mixamorig:LeftFoot")}}
};

const TCHAR* FingerNames[] = {
	TEXT("thumb_01"), TEXT("thumb_02"), TEXT("thumb_03"),
	TEXT("index_01"), TEXT("index_02"), TEXT("index_03"),
	TEXT("middle_01"), TEXT("middle_02"), TEXT("middle_03"),
	TEXT("ring_01"), TEXT("ring_02"), TEXT("ring_03"),
	TEXT("pinky_01"), TEXT("pinky_02"), TEXT("pinky_03")
};

constexpr int32 MannyFingerPoseCalibrationRevision = 2;

FName FindFirstBone(const FReferenceSkeleton& ReferenceSkeleton, const TArray<FName>& Candidates)
{
	for (const FName Candidate : Candidates)
	{
		if (ReferenceSkeleton.FindBoneIndex(Candidate) != INDEX_NONE)
		{
			return Candidate;
		}
	}
	return NAME_None;
}

FString MixamoFingerName(const TCHAR* Side, const TCHAR* FingerName)
{
	FString Stem(FingerName);
	FString Finger;
	if (Stem.StartsWith(TEXT("thumb")))
	{
		Finger = TEXT("Thumb");
	}
	else if (Stem.StartsWith(TEXT("index")))
	{
		Finger = TEXT("Index");
	}
	else if (Stem.StartsWith(TEXT("middle")))
	{
		Finger = TEXT("Middle");
	}
	else if (Stem.StartsWith(TEXT("ring")))
	{
		Finger = TEXT("Ring");
	}
	else
	{
		Finger = TEXT("Pinky");
	}
	const TCHAR Segment = Stem[Stem.Len() - 1];
	return FString::Printf(TEXT("mixamorig:%sHand%s%c"), Side, *Finger, Segment);
}

FName FindFingerBone(
	const FReferenceSkeleton& ReferenceSkeleton,
	const TCHAR* FingerName,
	const TCHAR* Side,
	const TCHAR* ShortSide
)
{
	const FString GenericName = MixamoFingerName(Side, FingerName).RightChop(10);
	return FindFirstBone(ReferenceSkeleton, {
		FName(*FString::Printf(TEXT("%s_%s"), FingerName, ShortSide)),
		FName(*GenericName),
		FName(*FString::Printf(TEXT("mixamorig:%s"), *GenericName))
	});
}

bool IsOrthonormalBasis(FVector PitchAxis, FVector YawAxis, FVector RollAxis)
{
	PitchAxis.Normalize();
	YawAxis.Normalize();
	RollAxis.Normalize();
	return !PitchAxis.IsNearlyZero() && !YawAxis.IsNearlyZero() && !RollAxis.IsNearlyZero() &&
		FMath::Abs(FVector::DotProduct(PitchAxis, YawAxis)) < 0.05f &&
		FMath::Abs(FVector::DotProduct(PitchAxis, RollAxis)) < 0.05f &&
		FMath::Abs(FVector::DotProduct(YawAxis, RollAxis)) < 0.05f;
}

FString MakeAssetName(FName ProfileId)
{
	FString Result = FString::Printf(TEXT("SP_%s"), *ProfileId.ToString());
	for (TCHAR& Character : Result)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
		{
			Character = TEXT('_');
		}
	}
	return Result;
}

const FLLMNPCFingerPoseProfile* FindFingerPose(
	const TArray<FLLMNPCFingerPoseProfile>& Poses,
	FName PoseId
)
{
	return Poses.FindByPredicate(
		[PoseId](const FLLMNPCFingerPoseProfile& Pose)
		{
			return
				Pose.PoseId == PoseId ||
				Pose.PoseId == FName(*FString::Printf(TEXT("fingers_%s"), *PoseId.ToString()));
		}
	);
}

FRotator ResolveFingerCalibration(
	const FLLMNPCFingerPoseProfile* ExistingPose,
	FName Semantic,
	const FRotator& Fallback
)
{
	if (ExistingPose)
	{
		if (const FRotator* ExistingRotation = ExistingPose->SemanticBoneRotations.Find(Semantic))
		{
			return *ExistingRotation;
		}
	}
	return Fallback;
}

void PopulateDefaultKinematicConstraints(ULLMNPCSkeletonProfile& Profile)
{
	for (const FLLMControlDefinition& Control : ULLMNPCControlManifest::GetBuiltInControls())
	{
		if (
			Control.SolverType != ELLMControlSolverType::AdditiveRotation &&
			Control.SolverType != ELLMControlSolverType::LocalOffset &&
			Control.SolverType != ELLMControlSolverType::TwoBoneIK &&
			Control.SolverType != ELLMControlSolverType::FingerPoseBlend
		)
		{
			continue;
		}
		if (Profile.FindControlConstraint(Control.ControlId))
		{
			continue;
		}

		FLLMNPCKinematicControlConstraint& Constraint =
			Profile.ControlConstraints.AddDefaulted_GetRef();
		Constraint.ControlId = Control.ControlId;
		const FString ControlName = Control.ControlId.ToString();
		if (ControlName.StartsWith(TEXT("head.")))
		{
			Constraint.MaxAngularSpeedDegreesPerSecond = 300.0f;
			Constraint.MaxAngularAccelerationDegreesPerSecondSquared = 1800.0f;
			Constraint.MaxAngularJerkDegreesPerSecondCubed = 12000.0f;
		}
		else if (
			ControlName.StartsWith(TEXT("chest.")) ||
			ControlName.Contains(TEXT("shoulder."))
		)
		{
			Constraint.MaxAngularSpeedDegreesPerSecond = 240.0f;
			Constraint.MaxAngularAccelerationDegreesPerSecondSquared = 1440.0f;
			Constraint.MaxAngularJerkDegreesPerSecondCubed = 9000.0f;
		}
		else if (Control.SolverType == ELLMControlSolverType::LocalOffset)
		{
			Constraint.MaxPositionSpeedCentimetersPerSecond = 180.0f;
			Constraint.MaxPositionAccelerationCentimetersPerSecondSquared = 1000.0f;
			Constraint.MaxPositionJerkCentimetersPerSecondCubed = 7000.0f;
		}
		else if (Control.SolverType == ELLMControlSolverType::FingerPoseBlend)
		{
			Constraint.MaxNormalizedSpeedPerSecond = 12.0f;
			Constraint.MaxNormalizedAccelerationPerSecondSquared = 480.0f;
			Constraint.MaxNormalizedJerkPerSecondCubed = 20000.0f;
		}
		else
		{
			Constraint.MaxAngularSpeedDegreesPerSecond = 540.0f;
			Constraint.MaxAngularAccelerationDegreesPerSecondSquared = 3600.0f;
			Constraint.MaxAngularJerkDegreesPerSecondCubed = 24000.0f;
		}
	}
}

void AddCollisionProxyIfMapped(
	ULLMNPCSkeletonProfile& Profile,
	FName ProxyId,
	ELLMNPCCollisionProxyShape Shape,
	FName AnchorSemantic,
	float Radius,
	float HalfHeight
)
{
	if (
		!Profile.SemanticBoneMap.Contains(AnchorSemantic) ||
		Profile.CollisionProxies.ContainsByPredicate(
			[ProxyId](const FLLMNPCCollisionProxyProfile& Proxy)
			{
				return Proxy.ProxyId == ProxyId;
			})
	)
	{
		return;
	}
	FLLMNPCCollisionProxyProfile& Proxy =
		Profile.CollisionProxies.AddDefaulted_GetRef();
	Proxy.ProxyId = ProxyId;
	Proxy.Shape = Shape;
	Proxy.AnchorBoneSemantic = AnchorSemantic;
	Proxy.RadiusCentimeters = Radius;
	Proxy.HalfHeightCentimeters = HalfHeight;
}

void PopulateDefaultCollisionProxies(ULLMNPCSkeletonProfile& Profile)
{
	AddCollisionProxyIfMapped(
		Profile,
		TEXT("head"),
		ELLMNPCCollisionProxyShape::Sphere,
		TEXT("head"),
		14.0f,
		0.0f
	);
	AddCollisionProxyIfMapped(
		Profile,
		TEXT("chest"),
		ELLMNPCCollisionProxyShape::Capsule,
		TEXT("chest"),
		18.0f,
		28.0f
	);
	AddCollisionProxyIfMapped(
		Profile,
		TEXT("pelvis"),
		ELLMNPCCollisionProxyShape::Capsule,
		TEXT("pelvis"),
		17.0f,
		24.0f
	);
	for (const FName Side : {FName(TEXT("right")), FName(TEXT("left"))})
	{
		AddCollisionProxyIfMapped(
			Profile,
			FName(*FString::Printf(TEXT("upperarm_%s"), *Side.ToString())),
			ELLMNPCCollisionProxyShape::Capsule,
			FName(*FString::Printf(TEXT("upperarm_%s"), *Side.ToString())),
			7.0f,
			17.0f
		);
		AddCollisionProxyIfMapped(
			Profile,
			FName(*FString::Printf(TEXT("lowerarm_%s"), *Side.ToString())),
			ELLMNPCCollisionProxyShape::Capsule,
			FName(*FString::Printf(TEXT("lowerarm_%s"), *Side.ToString())),
			6.0f,
			15.0f
		);
		AddCollisionProxyIfMapped(
			Profile,
			FName(*FString::Printf(TEXT("hand_%s"), *Side.ToString())),
			ELLMNPCCollisionProxyShape::Sphere,
			FName(*FString::Printf(TEXT("hand_%s"), *Side.ToString())),
			9.0f,
			0.0f
		);
	}
}
}

FLLMNPCSkeletonProfileAuthoringResult ULLMNPCSkeletonProfileAuthoringSubsystem::GenerateProfile(
	USkeleton* Skeleton,
	FName ProfileId,
	const FString& DestinationPackagePath,
	bool bEnableRuntimeAxisCalibration
)
{
	FLLMNPCSkeletonProfileAuthoringResult Result;
	if (!Skeleton || ProfileId.IsNone())
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_GENERATOR_INPUT_INVALID");
		Result.Message = TEXT("Skeleton and ProfileId are required.");
		return Result;
	}

	const FString AssetName = MakeAssetName(ProfileId);
	const FString PackageName = DestinationPackagePath / AssetName;
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_GENERATOR_PACKAGE_PATH_INVALID");
		Result.Message = PackageName;
		return Result;
	}
	if (FindObject<ULLMNPCSkeletonProfile>(nullptr, *(PackageName + TEXT(".") + AssetName)) ||
		LoadObject<ULLMNPCSkeletonProfile>(nullptr, *(PackageName + TEXT(".") + AssetName)))
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_GENERATOR_ASSET_EXISTS");
		Result.Message = PackageName;
		return Result;
	}
	FString Error;
	ULLMNPCSkeletonProfile* PreflightProfile = NewObject<ULLMNPCSkeletonProfile>();
	PreflightProfile->ProfileId = ProfileId;
	PreflightProfile->Skeleton = Skeleton;
	PreflightProfile->bApplyAxisCalibrationAtRuntime = bEnableRuntimeAxisCalibration;
	if (!PopulateGeneratedProfile(*PreflightProfile, *Skeleton, false, Error))
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_GENERATOR_MAPPING_FAILED");
		Result.Message = Error;
		return Result;
	}

	UPackage* Package = CreatePackage(*PackageName);
	ULLMNPCSkeletonProfile* Profile = NewObject<ULLMNPCSkeletonProfile>(
		Package,
		*AssetName,
		RF_Public | RF_Standalone | RF_Transactional
	);
	Profile->ProfileId = ProfileId;
	Profile->Skeleton = Skeleton;
	Profile->bApplyAxisCalibrationAtRuntime = bEnableRuntimeAxisCalibration;

	if (!PopulateGeneratedProfile(*Profile, *Skeleton, false, Error))
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_GENERATOR_MAPPING_FAILED");
		Result.Message = Error;
		return Result;
	}

	FAssetRegistryModule::AssetCreated(Profile);
	Profile->MarkPackageDirty();
	if (!SaveProfileAsset(*Profile, Error))
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_GENERATOR_SAVE_FAILED");
		Result.Message = Error;
		return Result;
	}

	Result.ProfileAsset = Profile;
	Result.AssetPath = Profile->GetPathName();
	Result.QualityReport = Profile->BuildQualityReport();
	if (!SaveQualityReport(Result.QualityReport, Result.ReportPath, Error))
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_GENERATOR_REPORT_SAVE_FAILED");
		Result.Message = Error;
		return Result;
	}

	Result.bSuccess = Result.QualityReport.bPassed;
	Result.ErrorCode = Result.bSuccess ? NAME_None : FName(TEXT("LLMNPC_PROFILE_GENERATOR_QUALITY_FAILED"));
	Result.Message = Result.bSuccess ? TEXT("Skeleton Profile generated.") : TEXT("Profile generated, but quality checks failed.");
	return Result;
}

FLLMNPCSkeletonProfileAuthoringResult ULLMNPCSkeletonProfileAuthoringSubsystem::RefreshGeneratedProfile(
	ULLMNPCSkeletonProfile* Profile,
	bool bPreserveExistingCalibration
)
{
	FLLMNPCSkeletonProfileAuthoringResult Result;
	USkeleton* Skeleton = Profile ? Profile->Skeleton.LoadSynchronous() : nullptr;
	if (!Profile || !Skeleton)
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_REFRESH_INPUT_INVALID");
		Result.Message = TEXT("Profile and Skeleton are required.");
		return Result;
	}

	FString Error;
	if (!PopulateGeneratedProfile(*Profile, *Skeleton, bPreserveExistingCalibration, Error) ||
		!SaveProfileAsset(*Profile, Error))
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_REFRESH_FAILED");
		Result.Message = Error;
		return Result;
	}

	Result.ProfileAsset = Profile;
	Result.AssetPath = Profile->GetPathName();
	Result.QualityReport = Profile->BuildQualityReport();
	Result.bSuccess = SaveQualityReport(Result.QualityReport, Result.ReportPath, Error) && Result.QualityReport.bPassed;
	Result.ErrorCode = Result.bSuccess ? NAME_None : FName(TEXT("LLMNPC_PROFILE_REFRESH_QUALITY_FAILED"));
	Result.Message = Result.bSuccess
		? TEXT("Skeleton Profile refreshed.")
		: (Error.IsEmpty() ? TEXT("Profile refreshed, but quality checks failed.") : Error);
	return Result;
}

FLLMNPCSkeletonProfileAuthoringResult ULLMNPCSkeletonProfileAuthoringSubsystem::SetAxisCalibration(
	ULLMNPCSkeletonProfile* Profile,
	FName SemanticBone,
	FVector PitchAxis,
	FVector YawAxis,
	FVector RollAxis,
	bool bEnableRuntimeAxisCalibration
)
{
	FLLMNPCSkeletonProfileAuthoringResult Result;
	if (!Profile || SemanticBone.IsNone() || !Profile->SemanticBoneMap.Contains(SemanticBone) ||
		!IsOrthonormalBasis(PitchAxis, YawAxis, RollAxis))
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_AXIS_CALIBRATION_INVALID");
		Result.Message = TEXT("Semantic bone and an orthonormal axis basis are required.");
		return Result;
	}

	FLLMNPCBoneAxisBasis& Basis = Profile->AxisBases.FindOrAdd(SemanticBone);
	Basis.PitchAxis = PitchAxis.GetSafeNormal();
	Basis.YawAxis = YawAxis.GetSafeNormal();
	Basis.RollAxis = RollAxis.GetSafeNormal();
	Profile->bApplyAxisCalibrationAtRuntime = bEnableRuntimeAxisCalibration;
	Profile->RefreshSkeletonSignature();
	Profile->MarkPackageDirty();

	FString Error;
	if (!SaveProfileAsset(*Profile, Error))
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_AXIS_CALIBRATION_SAVE_FAILED");
		Result.Message = Error;
		return Result;
	}
	return WriteQualityReport(Profile);
}

FLLMNPCSkeletonProfileAuthoringResult ULLMNPCSkeletonProfileAuthoringSubsystem::WriteQualityReport(
	ULLMNPCSkeletonProfile* Profile
)
{
	FLLMNPCSkeletonProfileAuthoringResult Result;
	if (!Profile)
	{
		Result.ErrorCode = TEXT("LLMNPC_PROFILE_REPORT_INPUT_INVALID");
		return Result;
	}

	Result.ProfileAsset = Profile;
	Result.AssetPath = Profile->GetPathName();
	Result.QualityReport = Profile->BuildQualityReport();
	FString Error;
	Result.bSuccess = SaveQualityReport(Result.QualityReport, Result.ReportPath, Error);
	Result.ErrorCode = Result.bSuccess ? NAME_None : FName(TEXT("LLMNPC_PROFILE_REPORT_SAVE_FAILED"));
	Result.Message = Result.bSuccess ? TEXT("Skeleton Profile quality report written.") : Error;
	return Result;
}

FString ULLMNPCSkeletonProfileAuthoringSubsystem::GetSkeletonProfileReportDirectory()
{
	return FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("LLMNPCActionLayer"),
		TEXT("Reports"),
		TEXT("SkeletonProfiles")
	);
}

bool ULLMNPCSkeletonProfileAuthoringSubsystem::PopulateGeneratedProfile(
	ULLMNPCSkeletonProfile& Profile,
	USkeleton& Skeleton,
	bool bPreserveExistingCalibration,
	FString& OutError
)
{
	OutError.Reset();
	const FReferenceSkeleton& ReferenceSkeleton = Skeleton.GetReferenceSkeleton();
	const TMap<FName, FLLMNPCBoneAxisBasis> ExistingAxes = Profile.AxisBases;
	const TArray<FLLMNPCFingerPoseProfile> ExistingFingerPoses = Profile.FingerPoses;
	const TArray<FLLMNPCIKChainProfile> ExistingIKChains = Profile.IKChains;
	Profile.SemanticBoneMap.Reset();
	Profile.AxisBases.Reset();

	for (const FSemanticBoneCandidates& Entry : CoreBoneCandidates)
	{
		const FName BoneName = FindFirstBone(ReferenceSkeleton, Entry.Candidates);
		if (BoneName.IsNone())
		{
			OutError = FString::Printf(TEXT("Required humanoid bone was not recognized: %s"), *Entry.Semantic.ToString());
			return false;
		}
		Profile.SemanticBoneMap.Add(Entry.Semantic, BoneName);
		if (bPreserveExistingCalibration)
		{
			if (const FLLMNPCBoneAxisBasis* Existing = ExistingAxes.Find(Entry.Semantic))
			{
				Profile.AxisBases.Add(Entry.Semantic, *Existing);
				continue;
			}
		}
		Profile.AxisBases.Add(Entry.Semantic, FLLMNPCBoneAxisBasis());
	}
	for (const FSemanticBoneCandidates& Entry : OptionalBoneCandidates)
	{
		const FName BoneName = FindFirstBone(ReferenceSkeleton, Entry.Candidates);
		if (!BoneName.IsNone())
		{
			Profile.SemanticBoneMap.Add(Entry.Semantic, BoneName);
		}
	}

	FLLMNPCPoseBoneBindings DefaultBindings;
	FLLMNPCFingerPoseProfile OpenPose;
	OpenPose.PoseId = TEXT("open");
	FLLMNPCFingerPoseProfile PointPose;
	PointPose.PoseId = TEXT("point");
	FLLMNPCFingerPoseProfile ContactPose;
	ContactPose.PoseId = TEXT("contact");
	FLLMNPCFingerPoseProfile RelaxedPose;
	RelaxedPose.PoseId = TEXT("relaxed");
	FLLMNPCFingerPoseProfile CurlPose;
	CurlPose.PoseId = TEXT("curl");
	const FLLMNPCFingerPoseProfile* ExistingOpen =
		FindFingerPose(ExistingFingerPoses, TEXT("open"));
	const FLLMNPCFingerPoseProfile* ExistingPoint =
		FindFingerPose(ExistingFingerPoses, TEXT("point"));
	const FLLMNPCFingerPoseProfile* ExistingContact =
		FindFingerPose(ExistingFingerPoses, TEXT("contact"));
	const FLLMNPCFingerPoseProfile* ExistingRelaxed =
		FindFingerPose(ExistingFingerPoses, TEXT("relaxed"));
	const FLLMNPCFingerPoseProfile* ExistingCurl =
		FindFingerPose(ExistingFingerPoses, TEXT("curl"));
	const bool bPreserveExtendedFingerPoses =
		bPreserveExistingCalibration &&
		(
			Profile.ProfileId != TEXT("ue5_manny.v1") ||
			Profile.FingerPoseCalibrationRevision >= MannyFingerPoseCalibrationRevision
		);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(FingerNames); ++Index)
	{
		const FName RightSemantic(*FString::Printf(TEXT("%s_right"), FingerNames[Index]));
		const FName LeftSemantic(*FString::Printf(TEXT("%s_left"), FingerNames[Index]));
		const FName RightBone = FindFingerBone(ReferenceSkeleton, FingerNames[Index], TEXT("Right"), TEXT("r"));
		const FName LeftBone = FindFingerBone(ReferenceSkeleton, FingerNames[Index], TEXT("Left"), TEXT("l"));
		if (!RightBone.IsNone())
		{
			Profile.SemanticBoneMap.Add(RightSemantic, RightBone);
			OpenPose.SemanticBoneRotations.Add(
				RightSemantic,
				ResolveFingerCalibration(
					bPreserveExistingCalibration ? ExistingOpen : nullptr,
					RightSemantic,
					DefaultBindings.RightFingerOpenRotations[Index]
				)
			);
			PointPose.SemanticBoneRotations.Add(
				RightSemantic,
				ResolveFingerCalibration(
					bPreserveExistingCalibration ? ExistingPoint : nullptr,
					RightSemantic,
					DefaultBindings.RightFingerPointRotations[Index]
				)
			);
			ContactPose.SemanticBoneRotations.Add(
				RightSemantic,
				ResolveFingerCalibration(
					bPreserveExistingCalibration ? ExistingContact : nullptr,
					RightSemantic,
					DefaultBindings.RightFingerContactRotations[Index]
				)
			);
			RelaxedPose.SemanticBoneRotations.Add(
				RightSemantic,
				ResolveFingerCalibration(
					bPreserveExtendedFingerPoses ? ExistingRelaxed : nullptr,
					RightSemantic,
					DefaultBindings.RightFingerRelaxedRotations[Index]
				)
			);
			CurlPose.SemanticBoneRotations.Add(
				RightSemantic,
				ResolveFingerCalibration(
					bPreserveExtendedFingerPoses ? ExistingCurl : nullptr,
					RightSemantic,
					DefaultBindings.RightFingerCurlRotations[Index]
				)
			);
		}
		if (!LeftBone.IsNone())
		{
			Profile.SemanticBoneMap.Add(LeftSemantic, LeftBone);
			OpenPose.SemanticBoneRotations.Add(
				LeftSemantic,
				ResolveFingerCalibration(
					bPreserveExistingCalibration ? ExistingOpen : nullptr,
					LeftSemantic,
					DefaultBindings.LeftFingerOpenRotations[Index]
				)
			);
			PointPose.SemanticBoneRotations.Add(
				LeftSemantic,
				ResolveFingerCalibration(
					bPreserveExistingCalibration ? ExistingPoint : nullptr,
					LeftSemantic,
					DefaultBindings.LeftFingerPointRotations[Index]
				)
			);
			ContactPose.SemanticBoneRotations.Add(
				LeftSemantic,
				ResolveFingerCalibration(
					bPreserveExistingCalibration ? ExistingContact : nullptr,
					LeftSemantic,
					DefaultBindings.LeftFingerContactRotations[Index]
				)
			);
			RelaxedPose.SemanticBoneRotations.Add(
				LeftSemantic,
				ResolveFingerCalibration(
					bPreserveExtendedFingerPoses ? ExistingRelaxed : nullptr,
					LeftSemantic,
					DefaultBindings.LeftFingerRelaxedRotations[Index]
				)
			);
			CurlPose.SemanticBoneRotations.Add(
				LeftSemantic,
				ResolveFingerCalibration(
					bPreserveExtendedFingerPoses ? ExistingCurl : nullptr,
					LeftSemantic,
					DefaultBindings.LeftFingerCurlRotations[Index]
				)
			);
		}
	}
	Profile.FingerPoses = {
		OpenPose,
		PointPose,
		ContactPose,
		RelaxedPose,
		CurlPose
	};

	Profile.IKChains.Reset();
	FLLMNPCIKChainProfile& RightArm = Profile.IKChains.AddDefaulted_GetRef();
	RightArm.ChainId = TEXT("right_arm");
	RightArm.RootBoneSemantic = TEXT("upperarm_right");
	RightArm.MidBoneSemantic = TEXT("lowerarm_right");
	RightArm.EndBoneSemantic = TEXT("hand_right");
	if (bPreserveExistingCalibration)
	{
		if (const FLLMNPCIKChainProfile* Existing = ExistingIKChains.FindByPredicate(
			[](const FLLMNPCIKChainProfile& Chain)
			{
				return Chain.ChainId == TEXT("right_arm");
			}))
		{
			RightArm = *Existing;
		}
	}
	FLLMNPCIKChainProfile& LeftArm = Profile.IKChains.AddDefaulted_GetRef();
	LeftArm.ChainId = TEXT("left_arm");
	LeftArm.RootBoneSemantic = TEXT("upperarm_left");
	LeftArm.MidBoneSemantic = TEXT("lowerarm_left");
	LeftArm.EndBoneSemantic = TEXT("hand_left");
	LeftArm.PoleDirectionCS *= -1.0f;
	if (bPreserveExistingCalibration)
	{
		if (const FLLMNPCIKChainProfile* Existing = ExistingIKChains.FindByPredicate(
			[](const FLLMNPCIKChainProfile& Chain)
			{
				return Chain.ChainId == TEXT("left_arm");
			}))
		{
			LeftArm = *Existing;
		}
	}
	RightArm.MinReachScale = FMath::Clamp(RightArm.MinReachScale, 0.05f, RightArm.MaxReachScale);
	LeftArm.MinReachScale = FMath::Clamp(LeftArm.MinReachScale, 0.05f, LeftArm.MaxReachScale);

	PopulateDefaultKinematicConstraints(Profile);
	PopulateDefaultCollisionProxies(Profile);
	Profile.StableGroundContactBoneSemantics.Reset();
	if (Profile.SemanticBoneMap.Contains(TEXT("foot_left")))
	{
		Profile.StableGroundContactBoneSemantics.Add(TEXT("foot_left"));
	}
	if (Profile.SemanticBoneMap.Contains(TEXT("foot_right")))
	{
		Profile.StableGroundContactBoneSemantics.Add(TEXT("foot_right"));
	}
	if (Profile.ProfileId == TEXT("ue5_manny.v1"))
	{
		Profile.SemanticVersion = TEXT("1.1.1");
		Profile.FingerPoseCalibrationRevision =
			MannyFingerPoseCalibrationRevision;
		Profile.UpperBodyConstraints.ValidationBaselineVersion =
			TEXT("manny.validation.baseline.v1");
	}

	Profile.Skeleton = &Skeleton;
	Profile.RefreshSkeletonSignature();
	Profile.MarkPackageDirty();
	return true;
}

bool ULLMNPCSkeletonProfileAuthoringSubsystem::SaveProfileAsset(
	ULLMNPCSkeletonProfile& Profile,
	FString& OutError
)
{
	OutError.Reset();
	UPackage* Package = Profile.GetOutermost();
	if (!Package)
	{
		OutError = TEXT("Profile package is missing.");
		return false;
	}

	const FString Filename = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, &Profile, *Filename, SaveArgs))
	{
		OutError = Filename;
		return false;
	}
	return true;
}

bool ULLMNPCSkeletonProfileAuthoringSubsystem::SaveQualityReport(
	const FLLMNPCSkeletonProfileQualityReport& Report,
	FString& OutPath,
	FString& OutError
)
{
	OutError.Reset();
	const FString Directory = GetSkeletonProfileReportDirectory();
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = Directory;
		return false;
	}

	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Report, Json, 0, 0, 0, nullptr, true))
	{
		OutError = TEXT("Could not serialize Skeleton Profile quality report.");
		return false;
	}
	OutPath = FPaths::Combine(
		Directory,
		FString::Printf(TEXT("%s.json"), *Report.ProfileId.ToString().Replace(TEXT("."), TEXT("_")))
	);
	if (!FFileHelper::SaveStringToFile(Json, *OutPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = OutPath;
		return false;
	}
	return true;
}
