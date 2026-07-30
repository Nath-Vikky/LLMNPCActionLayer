#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "LLMNPCArmIKSolver.h"
#include "LLMNPCMotionSampler.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeCompiler.h"
#include "MotionRecipe/LLMNPCMotionRecipeParser.h"
#include "MotionRecipe/LLMNPCMotionRecipeValidator.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"

namespace
{
constexpr uint32 ForwardN7CTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FString ValidBeckonRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "generated.beckon.procedural.001",
  "intent": "attract_attention",
  "duration": 1.8,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "hand.beckon",
      "side": "right",
      "start": 0.0,
      "end": 1.8,
      "target_slot": "primary",
      "parameters": {
        "amplitude": 0.7,
        "speed": 1.0,
        "cycles": 2,
        "curl_amount": 0.72,
        "reach": 0.58,
        "height": 0.55
      }
    }
  ]
}
)JSON");

ULLMNPCSkeletonProfile* LoadN7CMannyProfile()
{
	return LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
}

bool BuildN7CMannyCapability(
	FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot,
	FString& OutError
)
{
	const ULLMNPCSkeletonProfile* Profile = LoadN7CMannyProfile();
	if (!Profile)
	{
		OutError = TEXT("Manny Profile did not load.");
		return false;
	}
	const FLLMNPCSkeletonCapabilityBuildResult Result =
		FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
			*Profile,
			nullptr,
			FDateTime(2026, 7, 30, 12, 0, 0),
			OutSnapshot
		);
	if (!Result.bSucceeded)
	{
		OutError = FString::Join(Result.Errors, TEXT("; "));
		return false;
	}
	return true;
}

const FLLMMotionTrack* FindN7CTrack(
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

bool CompileN7CBeckon(
	FLLMMotionPlan& OutPlan,
	FLLMNPCCompiledRecipeMetadata& OutMetadata,
	FString& OutError
)
{
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	if (!BuildN7CMannyCapability(Capability, OutError))
	{
		return false;
	}
	FLLMNPCMotionRecipe Recipe;
	if (!FLLMNPCMotionRecipeParser::Parse(
		ValidBeckonRecipe,
		Recipe,
		OutError
	))
	{
		return false;
	}
	FLLMNPCMotionRecipeCompileContext Context;
	Context.ValidationContext.AllowedTargetSlots.Add(
		TEXT("primary")
	);
	Context.TargetBindings.Add(
		TEXT("primary"),
		TEXT("semantic_primary")
	);
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7CBeckonCapabilityTest,
	"LLMNPCActionLayer.ForwardN7C.ProceduralBeckon.CapabilityAndSchema",
	ForwardN7CTestFlags
)

bool FLLMNPCForwardN7CBeckonCapabilityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability builds"),
		BuildN7CMannyCapability(Capability, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}

	const FLLMNPCSemanticCapability* Beckon =
		Capability.Capabilities.FindByPredicate(
			[](const FLLMNPCSemanticCapability& Candidate)
			{
				return Candidate.CapabilityId ==
					TEXT("hand.beckon");
			}
		);
	TestNotNull(
		TEXT("Manny exposes hand.beckon"),
		Beckon
	);
	if (Beckon)
	{
		TestTrue(
			TEXT("Beckon is Authoring-only"),
			Beckon->bAuthoringOnly &&
				!Beckon->bRuntimeRecipeAllowed
		);
		TestTrue(
			TEXT("Beckon requires reach plus bounded relaxed and curl poses"),
			Beckon->Requires.Contains(TEXT("arm.reach")) &&
				Beckon->Requires.Contains(
					TEXT("hand.pose.relaxed")
				) &&
				Beckon->Requires.Contains(
					TEXT("hand.pose.curl")
				)
		);
		TestTrue(
			TEXT("Beckon is target-directed and supports either hand"),
			Beckon->TargetModes.Contains(TEXT("scene_target")) &&
				Beckon->SupportedSides.Contains(TEXT("right")) &&
				Beckon->SupportedSides.Contains(TEXT("left"))
		);
	}

	const FLLMNPCMotionPrimitiveDefinition* Definition =
		FLLMNPCMotionPrimitiveRegistry::Get().Find(
			TEXT("hand.beckon")
		);
	TestNotNull(
		TEXT("The Registry contains hand.beckon"),
		Definition
	);
	if (Definition)
	{
		TestTrue(
			TEXT("The Registry requires one semantic target"),
			Definition->bTargetRequired &&
				Definition->AllowedTargetModes.Contains(
					TEXT("target_slot")
				)
		);
		TestEqual(
			TEXT("The Registry selects the Manny Beckon Solver"),
			Definition->SolverId,
			FName(TEXT("solver.hand_beckon.manny.v1"))
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
		TEXT("The model can select hand.beckon and its semantic parameters"),
		SchemaJson.Contains(TEXT("hand.beckon")) &&
			SchemaJson.Contains(TEXT("curl_amount")) &&
			SchemaJson.Contains(TEXT("target_slot"))
	);
	for (
		const TCHAR* RestrictedValue :
		{
			TEXT("hand_r"),
			TEXT("lowerarm_r"),
			TEXT("right_hand.ik"),
			TEXT("right_fingers.curl"),
			TEXT("quaternion"),
			TEXT("euler")
		}
	)
	{
		TestFalse(
			FString::Printf(
				TEXT("The model Schema omits internal value '%s'"),
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
	FLLMNPCForwardN7CBeckonValidationTest,
	"LLMNPCActionLayer.ForwardN7C.ProceduralBeckon.ValidationPolicy",
	ForwardN7CTestFlags
)

bool FLLMNPCForwardN7CBeckonValidationTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	if (!BuildN7CMannyCapability(Capability, Error))
	{
		AddError(Error);
		return false;
	}

	FLLMNPCMotionRecipe Recipe;
	TestTrue(
		TEXT("The bounded Beckon Recipe parses"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidBeckonRecipe,
			Recipe,
			Error
		)
	);
	FLLMNPCMotionRecipeValidationContext Context;
	Context.AllowedTargetSlots.Add(TEXT("primary"));
	FLLMNPCMotionRecipeValidationResult Validation;
	TestTrue(
		TEXT("Manny accepts the bounded primary-target Beckon"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			Recipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("Right Beckon reserves only its arm and hand-pose channels"),
		Validation.RequiredChannels.Contains(
			TEXT("right_arm_ik")
		) &&
			Validation.RequiredChannels.Contains(
				TEXT("right_hand_pose")
			) &&
			!Validation.RequiredChannels.Contains(
				TEXT("left_arm_ik")
			) &&
			!Validation.RequiredChannels.Contains(
				TEXT("left_hand_pose")
			)
	);
	TestTrue(
		TEXT("Validation records exactly the primary target slot"),
		Validation.UsedTargetSlots.Num() == 1 &&
			Validation.UsedTargetSlots.Contains(
				TEXT("primary")
			)
	);

	FLLMNPCMotionRecipe MissingTargetRecipe;
	const FString MissingTargetJson =
		ValidBeckonRecipe.Replace(
			TEXT("      \"target_slot\": \"primary\",\n"),
			TEXT("")
		);
	TestTrue(
		TEXT("A targetless Beckon remains parseable"),
		FLLMNPCMotionRecipeParser::Parse(
			MissingTargetJson,
			MissingTargetRecipe,
			Error
		)
	);
	TestFalse(
		TEXT("A targetless Beckon fails closed"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			MissingTargetRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The missing-target failure is explicit"),
		Validation.ErrorCode.Contains(TEXT("TARGET_SLOT_REQUIRED"))
	);

	FLLMNPCMotionRecipe UnknownTargetRecipe;
	TestTrue(
		TEXT("An unknown-slot Beckon remains parseable"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidBeckonRecipe.Replace(
				TEXT("\"target_slot\": \"primary\""),
				TEXT("\"target_slot\": \"secondary\"")
			),
			UnknownTargetRecipe,
			Error
		)
	);
	TestFalse(
		TEXT("An unknown semantic target slot fails closed"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			UnknownTargetRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The unknown target failure is explicit"),
		Validation.ErrorCode.Contains(
			TEXT("TARGET_SLOT_NOT_ALLOWED")
		)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7CBeckonCompilerTest,
	"LLMNPCActionLayer.ForwardN7C.ProceduralBeckon.CompilerTargetAndMirror",
	ForwardN7CTestFlags
)

bool FLLMNPCForwardN7CBeckonCompilerTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMMotionPlan Plan;
	FLLMNPCCompiledRecipeMetadata Metadata;
	FString Error;
	TestTrue(
		*FString::Printf(
			TEXT("The Beckon Recipe compiles: %s"),
			*Error
		),
		CompileN7CBeckon(Plan, Metadata, Error)
	);
	if (!Error.IsEmpty() || Plan.Clip.Tracks.IsEmpty())
	{
		AddError(Error);
		return false;
	}
	TestEqual(
		TEXT("The Compiler identity records Beckon synthesis"),
		Metadata.CompilerVersion,
		FString(LLMNPCMotionRecipe::CompilerVersion)
	);
	TestEqual(
		TEXT("The Beckon Solver emits four bounded controls"),
		Metadata.PrimitiveMappings[0].GeneratedControlIds.Num(),
		4
	);
	TestEqual(
		TEXT("The Recipe placeholder is recorded deterministically"),
		Metadata.DynamicTargetBindings.FindRef(TEXT("primary")),
		FString(TEXT("semantic_primary"))
	);
	for (const FLLMMotionTrack& Track : Plan.Clip.Tracks)
	{
		TestTrue(
			FString::Printf(
				TEXT("Track '%s' carries the semantic target placeholder"),
				*Track.ControlId.ToString()
			),
			Track.TargetRef == TEXT("semantic_primary")
		);
		TestFalse(
			FString::Printf(
				TEXT("Track '%s' is not a raw hand Euler control"),
				*Track.ControlId.ToString()
			),
			Track.ControlId == TEXT("right_hand.pitch") ||
				Track.ControlId == TEXT("right_hand.yaw") ||
				Track.ControlId == TEXT("right_hand.roll")
		);
		TestFalse(
			TEXT("Right-hand authoring emits no left-side control"),
			Track.ControlId.ToString().StartsWith(TEXT("left_"))
		);
	}

	const FLLMMotionTrack* RightIK =
		FindN7CTrack(Plan.Clip, TEXT("right_hand.ik"));
	const FLLMMotionTrack* RightPalm =
		FindN7CTrack(
			Plan.Clip,
			TEXT("right_hand.palm_target")
		);
	const FLLMMotionTrack* Relaxed =
		FindN7CTrack(
			Plan.Clip,
			TEXT("right_fingers.relaxed")
		);
	const FLLMMotionTrack* Curl =
		FindN7CTrack(
			Plan.Clip,
			TEXT("right_fingers.curl")
		);
	TestNotNull(TEXT("Right Beckon IK exists"), RightIK);
	TestNotNull(TEXT("Right Beckon palm target exists"), RightPalm);
	TestNotNull(TEXT("Right relaxed-finger curve exists"), Relaxed);
	TestNotNull(TEXT("Right curl-finger curve exists"), Curl);
	if (!RightIK || !RightPalm || !Relaxed || !Curl)
	{
		return false;
	}
	TestTrue(
		TEXT("Reach and palm use semantic target solvers"),
		RightIK->TrackType == ELLMMotionTrackType::IKReach &&
			RightPalm->TrackType == ELLMMotionTrackType::LookAt
	);
	TestTrue(
		TEXT("Beckon reach is calibrated into Manny's bent-elbow solver band"),
		RightIK->Reach >= 0.18f &&
			RightIK->Reach <= 0.34f
	);
	TestTrue(
		TEXT("Right Beckon retains a small outward elbow bias"),
		RightIK->Offset.Y > 0.0f
	);
	TestEqual(
		TEXT("Relaxed and curl curves use matching phase keys"),
		Relaxed->FloatKeys.Num(),
		Curl->FloatKeys.Num()
	);
	for (int32 KeyIndex = 0;
		KeyIndex < Relaxed->FloatKeys.Num();
		++KeyIndex)
	{
		TestTrue(
			TEXT("Finger phase timestamps remain synchronized"),
			FMath::IsNearlyEqual(
				Relaxed->FloatKeys[KeyIndex].T,
				Curl->FloatKeys[KeyIndex].T
			)
		);
		TestTrue(
			TEXT("Every relaxed value is normalized"),
			Relaxed->FloatKeys[KeyIndex].V >= 0.0f &&
				Relaxed->FloatKeys[KeyIndex].V <= 1.0f
		);
		TestTrue(
			TEXT("Every curl value is normalized"),
			Curl->FloatKeys[KeyIndex].V >= 0.0f &&
				Curl->FloatKeys[KeyIndex].V <= 1.0f
		);
		if (
			KeyIndex > 0 &&
			KeyIndex < Relaxed->FloatKeys.Num() - 1
		)
		{
			TestTrue(
				TEXT("Active relaxed and curl phases remain complementary"),
				FMath::IsNearlyEqual(
					Relaxed->FloatKeys[KeyIndex].V +
						Curl->FloatKeys[KeyIndex].V,
					1.0f,
					0.001f
				)
			);
		}
	}

	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	TMap<FString, FLLMNPCTargetRuntimeSample> FullTargetSamples;
	FLLMNPCTargetRuntimeSample& FullTarget =
		FullTargetSamples.Add(TEXT("semantic_primary"));
	FullTarget.bValid = true;
	FullTarget.Alpha = 1.0f;
	FullTarget.LocationWS = FVector(100.0f, 0.0f, 0.0f);
	TMap<FString, FLLMNPCTargetRuntimeSample> FadingTargetSamples =
		FullTargetSamples;
	FadingTargetSamples.FindChecked(
		TEXT("semantic_primary")
	).Alpha = 0.25f;
	const float CurlPeakTime = Curl->FloatKeys[2].T;
	FLLMProceduralPoseSnapshot FullSnapshot;
	FLLMNPCMotionSampler::SampleClip(
		Plan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		CurlPeakTime,
		FullSnapshot,
		&FullTargetSamples
	);
	FLLMProceduralPoseSnapshot FadingSnapshot;
	FLLMNPCMotionSampler::SampleClip(
		Plan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		CurlPeakTime,
		FadingSnapshot,
		&FadingTargetSamples
	);
	TestTrue(
		TEXT("Target-loss alpha gates Beckon finger curl"),
		FullSnapshot.RightFingersCurl > 0.5f &&
			FMath::IsNearlyEqual(
				FadingSnapshot.RightFingersCurl,
				FullSnapshot.RightFingersCurl * 0.25f,
				0.001f
			)
	);

	ULLMNPCSkeletonProfile* Profile = LoadN7CMannyProfile();
	ULLMNPCMotionTemplate* PointTemplate =
		LoadObject<ULLMNPCMotionTemplate>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Point_Target_Manny_v1.MT_Point_Target_Manny_v1")
		);
	TestNotNull(TEXT("The Manny Profile remains available"), Profile);
	TestNotNull(
		TEXT("A published target template supplies valid catalog metadata"),
		PointTemplate
	);
	if (!Profile || !PointTemplate)
	{
		return false;
	}
	ULLMNPCMotionTemplate* BeckonTemplate =
		DuplicateObject<ULLMNPCMotionTemplate>(
			PointTemplate,
			GetTransientPackage()
		);
	BeckonTemplate->ProceduralClip = Plan.Clip;
	BeckonTemplate->ModifierPolicy.bAllowMirror = true;
	BeckonTemplate->Metadata.RequiredCapabilities = {
		TEXT("hand.beckon"),
		TEXT("arm.reach"),
		TEXT("hand.pose.relaxed"),
		TEXT("hand.pose.curl")
	};
	BeckonTemplate->Metadata.RequiredChannels = {
		TEXT("right_arm_ik"),
		TEXT("right_hand_pose")
	};
	BeckonTemplate->Metadata.CatalogContentHash.Reset();
	BeckonTemplate->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(
			*BeckonTemplate
		);
	FLLMNPCTemplateModifiers Modifiers;
	Modifiers.TargetRef = TEXT("runtime.actor");
	Modifiers.bMirror = true;
	FLLMMotionPlan MirroredPlan;
	TestTrue(
		*FString::Printf(
			TEXT("Template compilation replaces the target and mirrors safely: %s"),
			*Error
		),
		FLLMNPCTemplateCompiler::Compile(
			*BeckonTemplate,
			Modifiers,
			*Profile,
			MirroredPlan,
			Error
		)
	);
	const FLLMMotionTrack* LeftIK = FindN7CTrack(
		MirroredPlan.Clip,
		TEXT("left_hand.ik")
	);
	TestNotNull(
		TEXT("Right IK mirrors to left IK"),
		LeftIK
	);
	if (LeftIK)
	{
		TestTrue(
			TEXT("Mirrored Beckon moves the elbow bias to the left side"),
			LeftIK->Offset.Y < 0.0f
		);
	}
	TestNotNull(
		TEXT("Right curl mirrors to left curl"),
		FindN7CTrack(
			MirroredPlan.Clip,
			TEXT("left_fingers.curl")
		)
	);
	TestNull(
		TEXT("The mirrored plan contains no right IK"),
		FindN7CTrack(MirroredPlan.Clip, TEXT("right_hand.ik"))
	);
	for (const FLLMMotionTrack& Track : MirroredPlan.Clip.Tracks)
	{
		TestEqual(
			TEXT("Every mirrored target-aware track uses the runtime Actor ref"),
			Track.TargetRef,
			FString(TEXT("runtime.actor"))
		);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7CElbowContinuityTest,
	"LLMNPCActionLayer.ForwardN7C.ProceduralBeckon.ElbowContinuity",
	ForwardN7CTestFlags
)

bool FLLMNPCForwardN7CElbowContinuityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const FVector RootPosition = FVector::ZeroVector;
	const FVector CurrentJointPosition(20.0f, 12.0f, -18.0f);
	const FVector PoleDirection = FVector::BackwardVector;
	const FVector TargetBeforeCenter(55.0f, -0.5f, 0.0f);
	const FVector TargetAfterCenter(55.0f, 0.5f, 0.0f);
	const FVector BendBefore = (
		FLLMNPCArmIKSolver::BuildStableJointTarget(
			RootPosition,
			CurrentJointPosition,
			TargetBeforeCenter,
			PoleDirection,
			PoleDirection,
			65.0f
		) - RootPosition
	).GetSafeNormal();
	const FVector BendAfter = (
		FLLMNPCArmIKSolver::BuildStableJointTarget(
			RootPosition,
			CurrentJointPosition,
			TargetAfterCenter,
			PoleDirection,
			PoleDirection,
			65.0f
		) - RootPosition
	).GetSafeNormal();
	TestTrue(
		TEXT("Crossing a pole-aligned forward target does not flip the elbow"),
		FVector::DotProduct(BendBefore, BendAfter) > 0.98f
	);

	const FTransform OriginalUpper(
		FQuat::Identity,
		RootPosition
	);
	const FTransform OriginalLower(
		FQuat::Identity,
		CurrentJointPosition
	);
	const FTransform OriginalHand(
		FQuat::Identity,
		FVector(50.0f, 0.0f, -10.0f)
	);
	FTransform Upper = OriginalUpper;
	FTransform Lower = OriginalLower;
	FTransform Hand = OriginalHand;
	FLLMNPCArmIKSolver::SolveAtBlendedEffector(
		Upper,
		Lower,
		Hand,
		FVector(45.0f, 20.0f, 10.0f),
		PoleDirection,
		PoleDirection,
		0.01f
	);
	TestTrue(
		TEXT("A one-percent IK blend cannot teleport the elbow"),
		FVector::Dist(
			Lower.GetLocation(),
			OriginalLower.GetLocation()
		) < 1.0f
	);
	return true;
}

#endif
