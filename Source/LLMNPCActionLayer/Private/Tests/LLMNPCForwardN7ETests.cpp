#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Animation/Skeleton.h"
#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "LLMNPCArmIKSolver.h"
#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCMotionSampler.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeCompiler.h"
#include "MotionRecipe/LLMNPCMotionRecipeParser.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"

namespace
{
constexpr uint32 ForwardN7ETestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FString ValidThumbsUpRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "generated.thumbs_up.procedural.001",
  "intent": "agree",
  "duration": 1.6,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "hand.thumbs_up",
      "side": "right",
      "start": 0.0,
      "end": 1.6,
      "parameters": {
        "amplitude": 0.65,
        "height": 0.55
      }
    }
  ]
}
)JSON");

ULLMNPCSkeletonProfile* LoadForwardN7EMannyProfile()
{
	return LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
}

bool BuildForwardN7ECapability(
	FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot,
	FString& OutError
)
{
	const ULLMNPCSkeletonProfile* Profile =
		LoadForwardN7EMannyProfile();
	if (!Profile)
	{
		OutError = TEXT("Manny Profile did not load.");
		return false;
	}
	const FLLMNPCSkeletonCapabilityBuildResult Result =
		FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
			*Profile,
			nullptr,
			FDateTime(2026, 8, 1, 12, 0, 0),
			OutSnapshot
		);
	if (!Result.bSucceeded)
	{
		OutError = FString::Join(Result.Errors, TEXT("; "));
		return false;
	}
	return true;
}

const FLLMMotionTrack* FindForwardN7ETrack(
	const FLLMMotionClip& Clip,
	FName ControlId
)
{
	return Clip.Tracks.FindByPredicate(
		[ControlId](const FLLMMotionTrack& Track)
		{
			return Track.ControlId == ControlId;
		}
	);
}

bool CompileForwardN7EThumbsUp(
	FLLMMotionPlan& OutPlan,
	FLLMNPCCompiledRecipeMetadata& OutMetadata,
	FString& OutError
)
{
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	if (!BuildForwardN7ECapability(Capability, OutError))
	{
		return false;
	}
	FLLMNPCMotionRecipe Recipe;
	if (!FLLMNPCMotionRecipeParser::Parse(
		ValidThumbsUpRecipe,
		Recipe,
		OutError
	))
	{
		return false;
	}
	FLLMNPCMotionRecipeCompileContext Context;
	return FLLMNPCMotionRecipeCompiler::Compile(
		Recipe,
		Capability,
		FLLMNPCMotionPrimitiveRegistry::Get(),
		Context,
		OutPlan,
		OutMetadata,
		OutError
	);
}

FTransform BuildForwardN7EReferenceComponentTransform(
	const FReferenceSkeleton& ReferenceSkeleton,
	int32 BoneIndex
)
{
	const TArray<FTransform>& ReferencePose =
		ReferenceSkeleton.GetRefBonePose();
	FTransform Result = ReferencePose[BoneIndex];
	for (
		int32 ParentIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
		ParentIndex != INDEX_NONE;
		ParentIndex = ReferenceSkeleton.GetParentIndex(ParentIndex)
	)
	{
		Result = Result * ReferencePose[ParentIndex];
	}
	return Result;
}

struct FForwardN7EThumbGeometry
{
	FVector Thumb01Location = FVector::ZeroVector;
	FVector Thumb02Location = FVector::ZeroVector;
	FVector Thumb03Location = FVector::ZeroVector;
	FVector FirstSegmentDirection = FVector::ZeroVector;
	FVector SecondSegmentDirection = FVector::ZeroVector;
	FVector BaseToTipDirection = FVector::ZeroVector;
	FVector DistalBoneDirection = FVector::ZeroVector;
	float ChainLength = 0.0f;
	bool bIsValid = false;
};

FForwardN7EThumbGeometry EvaluateForwardN7EThumbGeometry(
	const FReferenceSkeleton& ReferenceSkeleton,
	bool bRightHand,
	const FRotator& Thumb01Delta,
	const FRotator& Thumb02Delta,
	const FRotator& Thumb03Delta
)
{
	FForwardN7EThumbGeometry Geometry;
	const TCHAR* Side = bRightHand ? TEXT("r") : TEXT("l");
	const int32 HandIndex = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("hand_%s"), Side))
	);
	const int32 Thumb01Index = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("thumb_01_%s"), Side))
	);
	const int32 Thumb02Index = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("thumb_02_%s"), Side))
	);
	const int32 Thumb03Index = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("thumb_03_%s"), Side))
	);
	if (
		HandIndex == INDEX_NONE ||
		Thumb01Index == INDEX_NONE ||
		Thumb02Index == INDEX_NONE ||
		Thumb03Index == INDEX_NONE
	)
	{
		return Geometry;
	}

	const TArray<FTransform>& ReferencePose =
		ReferenceSkeleton.GetRefBonePose();
	FTransform Thumb01Local = ReferencePose[Thumb01Index];
	FTransform Thumb02Local = ReferencePose[Thumb02Index];
	FTransform Thumb03Local = ReferencePose[Thumb03Index];
	Thumb01Local.SetRotation(
		FQuat(Thumb01Delta) * Thumb01Local.GetRotation()
	);
	Thumb02Local.SetRotation(
		FQuat(Thumb02Delta) * Thumb02Local.GetRotation()
	);
	Thumb03Local.SetRotation(
		FQuat(Thumb03Delta) * Thumb03Local.GetRotation()
	);

	const FTransform HandCS = BuildForwardN7EReferenceComponentTransform(
		ReferenceSkeleton,
		HandIndex
	);
	const FTransform Thumb01CS = Thumb01Local * HandCS;
	const FTransform Thumb02CS = Thumb02Local * Thumb01CS;
	const FTransform Thumb03CS = Thumb03Local * Thumb02CS;
	Geometry.Thumb01Location = Thumb01CS.GetLocation();
	Geometry.Thumb02Location = Thumb02CS.GetLocation();
	Geometry.Thumb03Location = Thumb03CS.GetLocation();
	Geometry.FirstSegmentDirection = (
		Geometry.Thumb02Location - Geometry.Thumb01Location
	).GetSafeNormal();
	Geometry.SecondSegmentDirection = (
		Geometry.Thumb03Location - Geometry.Thumb02Location
	).GetSafeNormal();
	Geometry.BaseToTipDirection = (
		Geometry.Thumb03Location - Geometry.Thumb01Location
	).GetSafeNormal();
	Geometry.DistalBoneDirection = Thumb03CS.GetRotation().RotateVector(
		bRightHand ? FVector::BackwardVector : FVector::ForwardVector
	).GetSafeNormal();
	Geometry.ChainLength =
		FVector::Distance(
			Geometry.Thumb01Location,
			Geometry.Thumb02Location
		) +
		FVector::Distance(
			Geometry.Thumb02Location,
			Geometry.Thumb03Location
		);
	Geometry.bIsValid =
		!Geometry.FirstSegmentDirection.IsNearlyZero() &&
		!Geometry.SecondSegmentDirection.IsNearlyZero() &&
		!Geometry.BaseToTipDirection.IsNearlyZero() &&
		!Geometry.DistalBoneDirection.IsNearlyZero() &&
		Geometry.ChainLength > KINDA_SMALL_NUMBER;
	return Geometry;
}

struct FForwardN7EHandBasis
{
	FTransform ChestTransform = FTransform::Identity;
	FTransform UpperArmTransform = FTransform::Identity;
	FTransform LowerArmTransform = FTransform::Identity;
	FTransform HandTransform = FTransform::Identity;
	FVector FingerDirection = FVector::ZeroVector;
	FVector AcrossPalmDirection = FVector::ZeroVector;
	FVector PalmNormal = FVector::ZeroVector;
	bool bIsValid = false;
};

FForwardN7EHandBasis GetForwardN7EReferenceHandBasis(
	const FReferenceSkeleton& ReferenceSkeleton,
	bool bRightHand
)
{
	FForwardN7EHandBasis Basis;
	const TCHAR* Side = bRightHand ? TEXT("r") : TEXT("l");
	const int32 ChestIndex = ReferenceSkeleton.FindBoneIndex(TEXT("spine_03"));
	const int32 UpperArmIndex = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("upperarm_%s"), Side))
	);
	const int32 LowerArmIndex = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("lowerarm_%s"), Side))
	);
	const int32 HandIndex = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("hand_%s"), Side))
	);
	const int32 IndexBaseIndex = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("index_01_%s"), Side))
	);
	const int32 MiddleTipIndex = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("middle_03_%s"), Side))
	);
	const int32 PinkyBaseIndex = ReferenceSkeleton.FindBoneIndex(
		FName(*FString::Printf(TEXT("pinky_01_%s"), Side))
	);
	if (
		ChestIndex == INDEX_NONE ||
		UpperArmIndex == INDEX_NONE ||
		LowerArmIndex == INDEX_NONE ||
		HandIndex == INDEX_NONE ||
		IndexBaseIndex == INDEX_NONE ||
		MiddleTipIndex == INDEX_NONE ||
		PinkyBaseIndex == INDEX_NONE
	)
	{
		return Basis;
	}
	Basis.ChestTransform = BuildForwardN7EReferenceComponentTransform(
		ReferenceSkeleton,
		ChestIndex
	);
	Basis.UpperArmTransform = BuildForwardN7EReferenceComponentTransform(
		ReferenceSkeleton,
		UpperArmIndex
	);
	Basis.LowerArmTransform = BuildForwardN7EReferenceComponentTransform(
		ReferenceSkeleton,
		LowerArmIndex
	);
	Basis.HandTransform = BuildForwardN7EReferenceComponentTransform(
		ReferenceSkeleton,
		HandIndex
	);
	const FVector HandLocation = Basis.HandTransform.GetLocation();
	Basis.FingerDirection = (
		BuildForwardN7EReferenceComponentTransform(
			ReferenceSkeleton,
			MiddleTipIndex
		).GetLocation() - HandLocation
	).GetSafeNormal();
	Basis.AcrossPalmDirection = (
		BuildForwardN7EReferenceComponentTransform(
			ReferenceSkeleton,
			IndexBaseIndex
		).GetLocation() -
		BuildForwardN7EReferenceComponentTransform(
			ReferenceSkeleton,
			PinkyBaseIndex
		).GetLocation()
	).GetSafeNormal();
	Basis.PalmNormal = FVector::CrossProduct(
		Basis.FingerDirection,
		Basis.AcrossPalmDirection
	).GetSafeNormal();
	Basis.bIsValid =
		!Basis.FingerDirection.IsNearlyZero() &&
		!Basis.AcrossPalmDirection.IsNearlyZero() &&
		!Basis.PalmNormal.IsNearlyZero();
	return Basis;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7EThumbCalibrationGeometryTest,
	"LLMNPCActionLayer.ForwardN7E.ProceduralThumbsUp.CalibrationGeometry",
	ForwardN7ETestFlags
)

bool FLLMNPCForwardN7EThumbCalibrationGeometryTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	ULLMNPCSkeletonProfile* Profile = LoadForwardN7EMannyProfile();
	TestNotNull(TEXT("The Manny Profile remains available"), Profile);
	USkeleton* Skeleton = Profile ? Profile->Skeleton.LoadSynchronous() : nullptr;
	TestNotNull(TEXT("The Manny Skeleton remains available"), Skeleton);
	if (!Skeleton)
	{
		return false;
	}

	const FReferenceSkeleton& ReferenceSkeleton =
		Skeleton->GetReferenceSkeleton();
	const FLLMNPCPoseBoneBindings CalibratedBindings =
		Profile->BuildPoseBoneBindings();
	for (const bool bRightHand : {true, false})
	{
		const TArray<FRotator>& Rotations = bRightHand
			? CalibratedBindings.RightFingerThumbsUpRotations
			: CalibratedBindings.LeftFingerThumbsUpRotations;
		if (!Rotations.IsValidIndex(2))
		{
			AddError(TEXT("The default Thumbs Up calibration is incomplete."));
			continue;
		}
		const FForwardN7EThumbGeometry ThumbGeometry =
			EvaluateForwardN7EThumbGeometry(
			ReferenceSkeleton,
			bRightHand,
			Rotations[0],
			Rotations[1],
			Rotations[2]
		);
		const FForwardN7EHandBasis HandBasis =
			GetForwardN7EReferenceHandBasis(
			ReferenceSkeleton,
			bRightHand
		);
		const FVector DesiredThumbDirection = (
			HandBasis.FingerDirection +
			HandBasis.AcrossPalmDirection * 0.75f
		).GetSafeNormal();
		TestTrue(
			bRightHand
				? TEXT("The right thumb extends upward and out from the fist")
				: TEXT("The left thumb extends upward and out from the fist"),
			ThumbGeometry.bIsValid &&
				HandBasis.bIsValid &&
				FVector::DotProduct(
					ThumbGeometry.BaseToTipDirection,
					DesiredThumbDirection
				) > 0.98f &&
				FVector::DotProduct(
					ThumbGeometry.BaseToTipDirection,
					HandBasis.AcrossPalmDirection
				) > 0.65f
		);
		TestTrue(
			bRightHand
				? TEXT("The right thumb joints form a straight extension")
				: TEXT("The left thumb joints form a straight extension"),
			ThumbGeometry.bIsValid &&
				FVector::DotProduct(
					ThumbGeometry.FirstSegmentDirection,
					ThumbGeometry.SecondSegmentDirection
				) > 0.98f
		);
		TestTrue(
			bRightHand
				? TEXT("The right distal thumb phalanx continues the extension")
				: TEXT("The left distal thumb phalanx continues the extension"),
			ThumbGeometry.bIsValid &&
				FVector::DotProduct(
					ThumbGeometry.SecondSegmentDirection,
					ThumbGeometry.DistalBoneDirection
				) > 0.99f
		);
	}
	TestTrue(
		TEXT("The thumb base is deliberately abducted away from the fist"),
		CalibratedBindings.RightFingerThumbsUpRotations[0].Roll < -10.0f
	);
	TestTrue(
		TEXT("Manny's parallel thumb axes use the same calibrated local deltas"),
		FMath::IsNearlyEqual(
			CalibratedBindings.RightFingerThumbsUpRotations[0].Pitch,
			CalibratedBindings.LeftFingerThumbsUpRotations[0].Pitch
		) &&
		FMath::IsNearlyEqual(
			CalibratedBindings.RightFingerThumbsUpRotations[0].Yaw,
			CalibratedBindings.LeftFingerThumbsUpRotations[0].Yaw
		) &&
		FMath::IsNearlyEqual(
			CalibratedBindings.RightFingerThumbsUpRotations[0].Roll,
			CalibratedBindings.LeftFingerThumbsUpRotations[0].Roll
		) &&
		FMath::IsNearlyEqual(
			CalibratedBindings.RightFingerThumbsUpRotations[1].Pitch,
			CalibratedBindings.LeftFingerThumbsUpRotations[1].Pitch
		) &&
		FMath::IsNearlyEqual(
			CalibratedBindings.RightFingerThumbsUpRotations[1].Yaw,
			CalibratedBindings.LeftFingerThumbsUpRotations[1].Yaw
		) &&
		FMath::IsNearlyEqual(
			CalibratedBindings.RightFingerThumbsUpRotations[1].Roll,
			CalibratedBindings.LeftFingerThumbsUpRotations[1].Roll
		) &&
		FMath::IsNearlyEqual(
			CalibratedBindings.RightFingerThumbsUpRotations[2].Pitch,
			CalibratedBindings.LeftFingerThumbsUpRotations[2].Pitch
		) &&
		FMath::IsNearlyEqual(
			CalibratedBindings.RightFingerThumbsUpRotations[2].Yaw,
			CalibratedBindings.LeftFingerThumbsUpRotations[2].Yaw
		) &&
		FMath::IsNearlyEqual(
			CalibratedBindings.RightFingerThumbsUpRotations[2].Roll,
			CalibratedBindings.LeftFingerThumbsUpRotations[2].Roll
		)
	);

	const FForwardN7EHandBasis RightHandBasis =
		GetForwardN7EReferenceHandBasis(ReferenceSkeleton, true);
	TestTrue(TEXT("The Manny right-hand basis is measurable"), RightHandBasis.bIsValid);
	if (!RightHandBasis.bIsValid)
	{
		return false;
	}
	FTransform SolvedUpperArm = RightHandBasis.UpperArmTransform;
	FTransform SolvedLowerArm = RightHandBasis.LowerArmTransform;
	FTransform SolvedHand = RightHandBasis.HandTransform;
	const FVector ThumbsUpAnchor =
		RightHandBasis.ChestTransform.GetLocation() +
		FVector(-28.0f, 20.0f, 20.0f);
	FLLMNPCArmIKSolver::SolveAtBlendedEffector(
		SolvedUpperArm,
		SolvedLowerArm,
		SolvedHand,
		ThumbsUpAnchor,
		FVector::BackwardVector,
		FVector::BackwardVector,
		1.0f
	);
	const FQuat SolvedHandDelta =
		SolvedHand.GetRotation() *
		RightHandBasis.HandTransform.GetRotation().Inverse();
	const FVector SolvedFingerDirection = SolvedHandDelta.RotateVector(
		RightHandBasis.FingerDirection
	).GetSafeNormal();
	const FVector SolvedPalmNormal = SolvedHandDelta.RotateVector(
		RightHandBasis.PalmNormal
	).GetSafeNormal();
	const FVector DesiredPalmNormal = FVector(1.0f, 0.0f, 0.0f);
	const float ForwardTiltRadians = FMath::DegreesToRadians(
		FLLMNPCArmIKSolver::ThumbsUpForwardTiltDegrees
	);
	const FVector DesiredFingerDirection = (
		FVector::UpVector * FMath::Cos(ForwardTiltRadians) +
		FVector(0.0f, 1.0f, 0.0f) * FMath::Sin(ForwardTiltRadians)
	).GetSafeNormal();
	const FQuat DesiredHandRotation =
		FLLMNPCArmIKSolver::BuildPalmFacingRotation(
			SolvedHand.GetRotation(),
			SolvedFingerDirection,
			SolvedPalmNormal,
			DesiredFingerDirection,
			DesiredPalmNormal
		);
	FLLMNPCArmIKSolver::ApplyConstrainedWristOrientation(
		SolvedLowerArm,
		SolvedHand,
		DesiredHandRotation,
		1.0f,
		FLLMNPCArmIKSolver::ThumbsUpMaxAxialTwistDegrees,
		FLLMNPCArmIKSolver::ThumbsUpMaxWristSwingDegrees
	);
	const FQuat AppliedHandDelta =
		SolvedHand.GetRotation() *
		RightHandBasis.HandTransform.GetRotation().Inverse();
	TestTrue(
		TEXT("The constrained Thumbs Up fist tilts forward after chest-anchor IK"),
		FVector::DotProduct(
			AppliedHandDelta.RotateVector(RightHandBasis.FingerDirection),
			DesiredFingerDirection
		) > 0.98f
	);
	TestTrue(
		TEXT("The constrained Thumbs Up palm faces inward after chest-anchor IK"),
		FVector::DotProduct(
			AppliedHandDelta.RotateVector(RightHandBasis.PalmNormal),
			DesiredPalmNormal
		) > 0.98f
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7EThumbsUpCapabilityTest,
	"LLMNPCActionLayer.ForwardN7E.ProceduralThumbsUp.CapabilityAndSchema",
	ForwardN7ETestFlags
)

bool FLLMNPCForwardN7EThumbsUpCapabilityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability builds"),
		BuildForwardN7ECapability(Capability, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}

	const FLLMNPCSemanticCapability* ThumbsUp =
		Capability.Capabilities.FindByPredicate(
			[](const FLLMNPCSemanticCapability& Candidate)
			{
				return Candidate.CapabilityId == TEXT("hand.thumbs_up");
			}
		);
	TestNotNull(TEXT("Manny exposes hand.thumbs_up"), ThumbsUp);
	if (ThumbsUp)
	{
		TestTrue(
			TEXT("Thumbs Up is Authoring-only"),
			ThumbsUp->bAuthoringOnly &&
				!ThumbsUp->bRuntimeRecipeAllowed
		);
		TestTrue(
			TEXT("Thumbs Up requires its calibrated hand pose"),
			ThumbsUp->Requires.Contains(TEXT("hand.pose.thumbs_up"))
		);
		TestTrue(
			TEXT("Thumbs Up supports either hand without a target"),
			ThumbsUp->SupportedSides.Contains(TEXT("right")) &&
				ThumbsUp->SupportedSides.Contains(TEXT("left")) &&
				ThumbsUp->TargetModes.Num() == 1 &&
				ThumbsUp->TargetModes.Contains(TEXT("none"))
		);
		TestTrue(
			TEXT("Thumbs Up owns only bounded private controls"),
			ThumbsUp->InternalControlIds.Contains(
				TEXT("right_hand.ik")
			) &&
				ThumbsUp->InternalControlIds.Contains(
					TEXT("right_fingers.thumbs_up")
				) &&
				ThumbsUp->InternalControlIds.Contains(
					TEXT("left_hand.ik")
				) &&
				ThumbsUp->InternalControlIds.Contains(
					TEXT("left_fingers.thumbs_up")
				)
		);
	}

	TestEqual(
		TEXT("The control manifest includes Thumbs Up support"),
		ULLMNPCControlManifest::GetBuiltInManifestVersion(),
		FString(TEXT("llmnpc.control_manifest.v4"))
	);
	const FLLMControlDefinition* PoseControl =
		ULLMNPCControlManifest::FindBuiltInControl(
			TEXT("right_fingers.thumbs_up")
		);
	const FLLMAnchorDefinition* Anchor =
		ULLMNPCControlManifest::FindBuiltInAnchor(
			TEXT("right_thumbs_up")
		);
	TestNotNull(TEXT("The right Thumbs Up pose control exists"), PoseControl);
	TestNotNull(TEXT("The right upper-chest anchor exists"), Anchor);
	if (PoseControl)
	{
		TestTrue(
			TEXT("The pose control accepts only normalized keyframes"),
			PoseControl->SolverType ==
				ELLMControlSolverType::FingerPoseBlend &&
				PoseControl->MinValue == 0.0f &&
				PoseControl->MaxValue == 1.0f &&
				PoseControl->AllowedTrackTypes.Contains(
					ELLMMotionTrackType::Keyframes
				)
		);
	}
	if (Anchor)
	{
		TestEqual(
			TEXT("The anchor remains relative to the upper chest"),
			Anchor->BoneName,
			FName(TEXT("spine_03"))
		);
	}

	const FLLMNPCMotionPrimitiveDefinition* Definition =
		FLLMNPCMotionPrimitiveRegistry::Get().Find(
			TEXT("hand.thumbs_up")
		);
	TestNotNull(TEXT("The Registry contains hand.thumbs_up"), Definition);
	if (Definition)
	{
		TestEqual(
			TEXT("Thumbs Up uses the constrained Manny Solver"),
			Definition->SolverId,
			FName(TEXT("solver.hand_thumbs_up.manny.v1"))
		);
		TestFalse(
			TEXT("Thumbs Up is target-independent"),
			Definition->bTargetRequired
		);
		TestTrue(
			TEXT("Thumbs Up exposes only amplitude and height"),
			Definition->ParameterSchemas.Num() == 2 &&
				Definition->FindParameterSchema(TEXT("amplitude")) &&
				Definition->FindParameterSchema(TEXT("height"))
		);
	}

	FString SchemaJson;
	TestTrue(
		TEXT("The Manny model Schema builds"),
		FLLMNPCMotionPrimitiveRegistry::Get().BuildModelSchemaJson(
			&Capability,
			SchemaJson,
			Error
		)
	);
	TestTrue(
		TEXT("The model sees only the semantic Thumbs Up primitive"),
		SchemaJson.Contains(TEXT("hand.thumbs_up"))
	);
	for (const TCHAR* RestrictedValue : {
		TEXT("right_fingers.thumbs_up"),
		TEXT("right_thumbs_up"),
		TEXT("thumb_03_r"),
		TEXT("quaternion"),
		TEXT("euler")
	})
	{
		TestFalse(
			FString::Printf(
				TEXT("The model Schema omits private value '%s'"),
				RestrictedValue
			),
			SchemaJson.Contains(
				RestrictedValue,
				ESearchCase::IgnoreCase
			)
		);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7EThumbsUpCompilerTest,
	"LLMNPCActionLayer.ForwardN7E.ProceduralThumbsUp.CompilerPoseAndMirror",
	ForwardN7ETestFlags
)

bool FLLMNPCForwardN7EThumbsUpCompilerTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMMotionPlan Plan;
	FLLMNPCCompiledRecipeMetadata Metadata;
	FString Error;
	TestTrue(
		*FString::Printf(TEXT("The Thumbs Up Recipe compiles: %s"), *Error),
		CompileForwardN7EThumbsUp(Plan, Metadata, Error)
	);
	if (!Error.IsEmpty() || Plan.Clip.Tracks.IsEmpty())
	{
		AddError(Error);
		return false;
	}
	TestEqual(
		TEXT("Thumbs Up emits exactly two bounded controls"),
		Metadata.PrimitiveMappings[0].GeneratedControlIds.Num(),
		2
	);
	TestTrue(
		TEXT("Target-independent compilation emits no bindings"),
		Metadata.DynamicTargetBindings.IsEmpty()
	);

	const FLLMMotionTrack* IK =
		FindForwardN7ETrack(Plan.Clip, TEXT("right_hand.ik"));
	const FLLMMotionTrack* Pose =
		FindForwardN7ETrack(
			Plan.Clip,
			TEXT("right_fingers.thumbs_up")
		);
	TestNotNull(TEXT("Thumbs Up IK exists"), IK);
	TestNotNull(TEXT("Thumbs Up calibrated hand pose exists"), Pose);
	if (!IK || !Pose)
	{
		return false;
	}
	TestTrue(
		TEXT("The hand fully reaches its chest-relative Anchor track"),
		IK->TrackType == ELLMMotionTrackType::Anchor &&
			IK->Anchor == TEXT("right_thumbs_up") &&
			FMath::IsNearlyEqual(IK->Strength, 1.0f)
	);
	TestTrue(
		TEXT("The calibrated pose uses a shaped normalized curve"),
		Pose->TrackType == ELLMMotionTrackType::Keyframes &&
			Pose->FloatKeys.Num() >= 4
	);
	float PeakPoseWeight = 0.0f;
	for (const FLLMMotionKeyFloat& Key : Pose->FloatKeys)
	{
		PeakPoseWeight = FMath::Max(PeakPoseWeight, Key.V);
	}
	TestTrue(
		TEXT("The semantic hand shape reaches its full calibrated pose"),
		FMath::IsNearlyEqual(PeakPoseWeight, 1.0f)
	);
	for (const FLLMMotionTrack& Track : Plan.Clip.Tracks)
	{
		TestTrue(
			TEXT("Thumbs Up tracks carry no scene target"),
			Track.TargetRef.IsEmpty()
		);
		TestFalse(
			TEXT("Right-hand authoring emits no left-side control"),
			Track.ControlId.ToString().StartsWith(TEXT("left_"))
		);
	}

	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	FLLMProceduralPoseSnapshot Snapshot;
	FLLMNPCMotionSampler::SampleClip(
		Plan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		0.8f,
		Snapshot
	);
	TestTrue(
		TEXT("The Sampler exposes the IK and calibrated Thumbs Up pose"),
		Snapshot.RightHandIKAlpha > 0.5f &&
			Snapshot.RightFingersThumbsUp > 0.5f
	);
	TestTrue(
		TEXT("The opposite hand remains untouched"),
		Snapshot.LeftHandIKAlpha == 0.0f &&
			Snapshot.LeftFingersThumbsUp == 0.0f
	);

	ULLMNPCSkeletonProfile* Profile = LoadForwardN7EMannyProfile();
	TestNotNull(TEXT("The Manny Profile remains available"), Profile);
	if (!Profile)
	{
		return false;
	}
	TestEqual(
		TEXT("Manny stores the corrected Thumbs Up calibration revision"),
		Profile->FingerPoseCalibrationRevision,
		6
	);
	const FLLMNPCFingerPoseProfile* ProfilePose =
		Profile->FingerPoses.FindByPredicate(
			[](const FLLMNPCFingerPoseProfile& Candidate)
			{
				return Candidate.PoseId == TEXT("thumbs_up");
			}
		);
	TestNotNull(TEXT("Manny stores a dedicated Thumbs Up pose"), ProfilePose);
	if (ProfilePose)
	{
		TestEqual(
			TEXT("The pose calibrates all fingers on both hands"),
			ProfilePose->SemanticBoneRotations.Num(),
			30
		);
	}
	const FLLMNPCPoseBoneBindings Bindings =
		Profile->BuildPoseBoneBindings();
	TestEqual(
		TEXT("Right Thumbs Up bindings cover all 15 finger bones"),
		Bindings.RightFingerThumbsUpRotations.Num(),
		15
	);
	TestEqual(
		TEXT("Left Thumbs Up bindings cover all 15 finger bones"),
		Bindings.LeftFingerThumbsUpRotations.Num(),
		15
	);
	TestTrue(
		TEXT("The thumb stays more extended than the curled index finger"),
		Bindings.RightFingerThumbsUpRotations[1].GetManhattanDistance(
			FRotator::ZeroRotator
		) <
		Bindings.RightFingerThumbsUpRotations[4].GetManhattanDistance(
			FRotator::ZeroRotator
		)
	);
	TestTrue(
		TEXT("The shipped right thumb base abducts away from the fist"),
		Bindings.RightFingerThumbsUpRotations[0].Roll < -10.0f
	);

	ULLMNPCMotionTemplate* BaselineTemplate =
		LoadObject<ULLMNPCMotionTemplate>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Point_Target_Manny_v1.MT_Point_Target_Manny_v1")
		);
	TestNotNull(TEXT("A Manny template baseline remains available"), BaselineTemplate);
	if (!BaselineTemplate)
	{
		return false;
	}
	ULLMNPCMotionTemplate* ThumbsUpTemplate =
		DuplicateObject<ULLMNPCMotionTemplate>(
			BaselineTemplate,
			GetTransientPackage()
		);
	ThumbsUpTemplate->ProceduralClip = Plan.Clip;
	ThumbsUpTemplate->ModifierPolicy.bAllowMirror = true;
	ThumbsUpTemplate->ModifierPolicy.bEnableDynamicTargetTracking = false;
	ThumbsUpTemplate->Metadata.bRequiresTarget = false;
	ThumbsUpTemplate->Metadata.TargetCategoryTags.Reset();
	ThumbsUpTemplate->Metadata.SpatialRequirementTags.Remove(
		TEXT("target_required")
	);
	ThumbsUpTemplate->Metadata.RequiredCapabilities = {
		TEXT("hand.thumbs_up"),
		TEXT("hand.pose.thumbs_up")
	};
	ThumbsUpTemplate->Metadata.RequiredChannels = {
		TEXT("right_arm_ik"),
		TEXT("right_hand_pose")
	};
	ThumbsUpTemplate->Metadata.CatalogContentHash.Reset();
	ThumbsUpTemplate->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(
			*ThumbsUpTemplate
		);
	FLLMNPCTemplateModifiers Modifiers;
	Modifiers.bMirror = true;
	FLLMMotionPlan MirroredPlan;
	Error.Reset();
	TestTrue(
		*FString::Printf(TEXT("Thumbs Up mirrors safely: %s"), *Error),
		FLLMNPCTemplateCompiler::Compile(
			*ThumbsUpTemplate,
			Modifiers,
			*Profile,
			MirroredPlan,
			Error
		)
	);
	const FLLMMotionTrack* LeftIK =
		FindForwardN7ETrack(MirroredPlan.Clip, TEXT("left_hand.ik"));
	TestNotNull(TEXT("Right IK mirrors to left IK"), LeftIK);
	TestNotNull(
		TEXT("Right Thumbs Up pose mirrors to the left pose"),
		FindForwardN7ETrack(
			MirroredPlan.Clip,
			TEXT("left_fingers.thumbs_up")
		)
	);
	TestNull(
		TEXT("The mirrored plan contains no right Thumbs Up pose"),
		FindForwardN7ETrack(
			MirroredPlan.Clip,
			TEXT("right_fingers.thumbs_up")
		)
	);
	if (LeftIK)
	{
		TestEqual(
			TEXT("The anchor mirrors semantically"),
			LeftIK->Anchor,
			FName(TEXT("left_thumbs_up"))
		);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7EThumbsUpRuntimeMergeTest,
	"LLMNPCActionLayer.ForwardN7E.ProceduralThumbsUp.RuntimePoseMerge",
	ForwardN7ETestFlags
)

bool FLLMNPCForwardN7EThumbsUpRuntimeMergeTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMProceduralPoseSnapshot Merged;
	FLLMProceduralPoseSnapshot Sampled;
	Sampled.RightFingersThumbsUp = 0.75f;
	Sampled.LeftFingersThumbsUp = 0.65f;
	Sampled.RightHandPalmUp = 0.55f;
	Sampled.LeftHandPalmUp = 0.45f;

	ULLMNPCMotionComponent::MergeSnapshot(Merged, Sampled);

	TestEqual(
		TEXT("The right Thumbs Up pose survives runtime composition"),
		Merged.RightFingersThumbsUp,
		Sampled.RightFingersThumbsUp
	);
	TestEqual(
		TEXT("The left Thumbs Up pose survives runtime composition"),
		Merged.LeftFingersThumbsUp,
		Sampled.LeftFingersThumbsUp
	);
	TestEqual(
		TEXT("The right palm pose survives runtime composition"),
		Merged.RightHandPalmUp,
		Sampled.RightHandPalmUp
	);
	TestEqual(
		TEXT("The left palm pose survives runtime composition"),
		Merged.LeftHandPalmUp,
		Sampled.LeftHandPalmUp
	);
	return true;
}

#endif
