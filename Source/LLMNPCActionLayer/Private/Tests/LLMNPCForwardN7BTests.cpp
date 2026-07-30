#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "LLMNPCArmIKSolver.h"
#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionSampler.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeCompiler.h"
#include "MotionRecipe/LLMNPCMotionRecipeParser.h"
#include "MotionRecipe/LLMNPCMotionRecipeValidator.h"
#include "Sandbox/LLMNPCAuthoringSandbox.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

namespace
{
constexpr uint32 ForwardN7BTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FString ValidClapRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "generated.clap.procedural.001",
  "intent": "applaud",
  "duration": 1.8,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "hands.contact",
      "side": "none",
      "start": 0.0,
      "end": 1.8,
      "parameters": {
        "amplitude": 0.75,
        "speed": 1.0,
        "cycles": 2,
        "contact_height": 0.55,
        "separation": 0.65,
        "palm_openness": 0.9
      }
    }
  ]
}
)JSON");

ULLMNPCSkeletonProfile* LoadN7BMannyProfile()
{
	return LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
}

bool BuildN7BMannyCapability(
	FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot,
	FString& OutError
)
{
	const ULLMNPCSkeletonProfile* Profile = LoadN7BMannyProfile();
	if (!Profile)
	{
		OutError = TEXT("Manny Profile did not load.");
		return false;
	}
	const FLLMNPCSkeletonCapabilityBuildResult Result =
		FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
			*Profile,
			nullptr,
			FDateTime(2026, 7, 28, 12, 0, 0),
			OutSnapshot
		);
	if (!Result.bSucceeded)
	{
		OutError = FString::Join(Result.Errors, TEXT("; "));
		return false;
	}
	return true;
}

const FLLMMotionTrack* FindForwardN7BTrack(
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7BClapCapabilityTest,
	"LLMNPCActionLayer.ForwardN7B.ProceduralClap.CapabilityAndSchema",
	ForwardN7BTestFlags
)

bool FLLMNPCForwardN7BClapCapabilityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability builds"),
		BuildN7BMannyCapability(Capability, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}

	const FLLMNPCSemanticCapability* HandsContact =
		Capability.Capabilities.FindByPredicate(
			[](const FLLMNPCSemanticCapability& Candidate)
			{
				return Candidate.CapabilityId ==
					TEXT("hands.contact");
			}
		);
	TestNotNull(
		TEXT("Manny exposes the bilateral hands.contact capability"),
		HandsContact
	);
	if (HandsContact)
	{
		TestTrue(
			TEXT("hands.contact is Authoring-only"),
			HandsContact->bAuthoringOnly &&
				!HandsContact->bRuntimeRecipeAllowed
		);
		TestTrue(
			TEXT("hands.contact requires the bounded open-hand pose"),
			HandsContact->Requires.Contains(TEXT("hand.pose.open"))
		);
		TestTrue(
			TEXT("hands.contact conflicts with either occupied hand"),
			HandsContact->ConflictsWith.Contains(
				TEXT("right_hand_busy")
			) &&
				HandsContact->ConflictsWith.Contains(
					TEXT("left_hand_busy")
				)
		);
	}

	FString SchemaJson;
	TestTrue(
		TEXT("The model schema builds for Manny"),
		FLLMNPCMotionPrimitiveRegistry::Get().BuildModelSchemaJson(
			&Capability,
			SchemaJson,
			Error
		)
	);
	TestTrue(
		TEXT("The model can select hands.contact"),
		SchemaJson.Contains(TEXT("hands.contact"))
	);
	TestTrue(
		TEXT("The schema annotates the bounded contact duration"),
		SchemaJson.Contains(TEXT("\"x-min-duration-seconds\": 0.8")) &&
			SchemaJson.Contains(TEXT("\"x-max-duration-seconds\": 3.2"))
	);
	for (
		const TCHAR* RestrictedValue :
		{
			TEXT("right_clap"),
			TEXT("left_clap"),
			TEXT("clap_center"),
			TEXT("hand_r"),
			TEXT("hand_l"),
			TEXT("quaternion"),
			TEXT("target_ref")
		}
	)
	{
		TestFalse(
			FString::Printf(
				TEXT("The model schema omits internal value '%s'"),
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
	FLLMNPCForwardN7BContactFingerPoseTest,
	"LLMNPCActionLayer.ForwardN7B.ProceduralClap.ContactFingerPose",
	ForwardN7BTestFlags
)

bool FLLMNPCForwardN7BContactFingerPoseTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* Profile = LoadN7BMannyProfile();
	TestNotNull(TEXT("The shipped Manny Profile loads"), Profile);
	if (!Profile)
	{
		return false;
	}

	const FLLMNPCPoseBoneBindings Bindings =
		Profile->BuildPoseBoneBindings();
	TestEqual(
		TEXT("The right contact hand calibrates every finger joint"),
		Bindings.RightFingerContactRotations.Num(),
		15
	);
	TestEqual(
		TEXT("The left contact hand calibrates every finger joint"),
		Bindings.LeftFingerContactRotations.Num(),
		15
	);
	TestTrue(
		TEXT("The contact pose fully extends the right index base"),
		Bindings.RightFingerContactRotations.IsValidIndex(3) &&
			Bindings.RightFingerContactRotations[3].Yaw > 25.0f
	);
	TestTrue(
		TEXT("The contact pose fully extends the left index base"),
		Bindings.LeftFingerContactRotations.IsValidIndex(3) &&
			Bindings.LeftFingerContactRotations[3].Yaw > 20.0f
	);
	TestTrue(
		TEXT("The contact pose is isolated from the reviewed Wave open pose"),
		Bindings.RightFingerOpenRotations.IsValidIndex(3) &&
			Bindings.LeftFingerOpenRotations.IsValidIndex(3) &&
			FMath::IsNearlyEqual(
				Bindings.RightFingerOpenRotations[3].Yaw,
				7.0f
			) &&
			FMath::IsNearlyEqual(
				Bindings.LeftFingerOpenRotations[3].Yaw,
				-7.0f
			)
	);
	TestNotNull(
		TEXT("The right contact pose is a manifest control"),
		ULLMNPCControlManifest::FindBuiltInControl(
			TEXT("right_fingers.contact")
		)
	);
	TestNotNull(
		TEXT("The left contact pose is a manifest control"),
		ULLMNPCControlManifest::FindBuiltInControl(
			TEXT("left_fingers.contact")
		)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7BClapPolicyTest,
	"LLMNPCActionLayer.ForwardN7B.ProceduralClap.ValidationPolicy",
	ForwardN7BTestFlags
)

bool FLLMNPCForwardN7BClapPolicyTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability builds"),
		BuildN7BMannyCapability(Capability, Error)
	);
	FLLMNPCMotionRecipe Recipe;
	TestTrue(
		TEXT("The bounded Clap Recipe parses"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidClapRecipe,
			Recipe,
			Error
		)
	);
	FLLMNPCMotionRecipeValidationContext Context;
	FLLMNPCMotionRecipeValidationResult Validation;
	TestTrue(
		TEXT("Manny accepts the bounded Clap Recipe"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			Recipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The Clap reserves both arm and hand-pose channels"),
		Validation.RequiredChannels.Contains(
			TEXT("left_arm_ik")
		) &&
			Validation.RequiredChannels.Contains(
				TEXT("right_arm_ik")
			) &&
			Validation.RequiredChannels.Contains(
				TEXT("left_hand_pose")
			) &&
			Validation.RequiredChannels.Contains(
				TEXT("right_hand_pose")
			)
	);
	TestTrue(
		TEXT("The target-independent Clap has no scene target slots"),
		Validation.UsedTargetSlots.IsEmpty()
	);

	FLLMNPCMotionRecipe BusyRecipe;
	TestTrue(
		TEXT("The Clap reparses for occupied-hand validation"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidClapRecipe,
			BusyRecipe,
			Error
		)
	);
	Context.ActiveBlockedStates.Add(TEXT("right_hand_busy"));
	TestFalse(
		TEXT("An occupied right hand blocks the bilateral Clap"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			BusyRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The occupied-hand rejection is explicit"),
		Validation.ErrorCode.Contains(TEXT("STATE_BLOCKED"))
	);

	FLLMNPCMotionRecipe OutOfRangeRecipe;
	TestTrue(
		TEXT("An out-of-range cycle count remains parseable"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidClapRecipe.Replace(
				TEXT("\"cycles\": 2"),
				TEXT("\"cycles\": 4")
			),
			OutOfRangeRecipe,
			Error
		)
	);
	Context.ActiveBlockedStates.Reset();
	TestFalse(
		TEXT("The Registry rejects an unsafe cycle count"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			OutOfRangeRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The range failure identifies cycles"),
		Validation.ErrorCode.Contains(
			TEXT("PARAMETER_OUT_OF_RANGE:cycles")
		)
	);

	FString ShortRecipeJson = ValidClapRecipe.Replace(
		TEXT("\"duration\": 1.8"),
		TEXT("\"duration\": 0.6")
	);
	ShortRecipeJson = ShortRecipeJson.Replace(
		TEXT("\"end\": 1.8"),
		TEXT("\"end\": 0.6")
	);
	FLLMNPCMotionRecipe ShortRecipe;
	TestTrue(
		TEXT("A too-short Clap remains parseable"),
		FLLMNPCMotionRecipeParser::Parse(
			ShortRecipeJson,
			ShortRecipe,
			Error
		)
	);
	TestFalse(
		TEXT("The Registry rejects a Clap below 0.8 seconds"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			ShortRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The minimum-duration failure is explicit"),
		Validation.ErrorCode.Contains(
			TEXT("DURATION_BELOW_MINIMUM")
		)
	);

	FString LongRecipeJson = ValidClapRecipe.Replace(
		TEXT("\"duration\": 1.8"),
		TEXT("\"duration\": 3.4")
	);
	LongRecipeJson = LongRecipeJson.Replace(
		TEXT("\"end\": 1.8"),
		TEXT("\"end\": 3.4")
	);
	FLLMNPCMotionRecipe LongRecipe;
	TestTrue(
		TEXT("An overlong Clap remains parseable"),
		FLLMNPCMotionRecipeParser::Parse(
			LongRecipeJson,
			LongRecipe,
			Error
		)
	);
	TestFalse(
		TEXT("The Registry rejects a Clap above 3.2 seconds"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			LongRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The maximum-duration failure is explicit"),
		Validation.ErrorCode.Contains(
			TEXT("PRIMITIVE_DURATION_EXCEEDED")
		)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7BClapCompilerTest,
	"LLMNPCActionLayer.ForwardN7B.ProceduralClap.CompilerAndSampling",
	ForwardN7BTestFlags
)

bool FLLMNPCForwardN7BClapCompilerTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability builds"),
		BuildN7BMannyCapability(Capability, Error)
	);
	FLLMNPCMotionRecipe Recipe;
	TestTrue(
		TEXT("The bounded Clap Recipe parses"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidClapRecipe,
			Recipe,
			Error
		)
	);
	FLLMNPCMotionRecipeCompileContext Context;
	FLLMMotionPlan Plan;
	FLLMNPCCompiledRecipeMetadata Metadata;
	TestTrue(
		TEXT("hands.contact compiles to a bounded Motion Plan"),
		FLLMNPCMotionRecipeCompiler::Compile(
			Recipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Plan,
			Metadata,
			Error
		)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}
	TestEqual(
		TEXT("The compiler version records contact synthesis"),
		Metadata.CompilerVersion,
		FString(TEXT("llmnpc.motion_recipe_compiler.v4"))
	);
	TestEqual(
		TEXT("The Registry selects the internal contact Solver"),
		Metadata.PrimitiveMappings[0].SolverId,
		FName(TEXT("solver.hands_contact.manny.v3"))
	);
	TestEqual(
		TEXT("The contact Solver emits eight bounded controls"),
		Metadata.PrimitiveMappings[0].GeneratedControlIds.Num(),
		8
	);
	TestTrue(
		TEXT("The contact Solver uses explicit bilateral palm-facing controls"),
		Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
			TEXT("right_hand.palm_facing")
		) &&
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("left_hand.palm_facing")
			)
	);
	TestTrue(
		TEXT("The contact Solver uses the calibrated bilateral contact pose"),
		Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
			TEXT("right_fingers.contact")
		) &&
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("left_fingers.contact")
			) &&
			!Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("right_fingers.open")
			) &&
			!Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("left_fingers.open")
			)
	);
	for (const FLLMMotionTrack& Track : Plan.Clip.Tracks)
	{
		TestTrue(
			FString::Printf(
				TEXT("Track '%s' does not embed a scene TargetRef"),
				*Track.ControlId.ToString()
			),
			Track.TargetRef.IsEmpty()
		);
		TestFalse(
			FString::Printf(
				TEXT("Track '%s' is not a raw hand Euler control"),
				*Track.ControlId.ToString()
			),
			Track.ControlId == TEXT("right_hand.pitch") ||
				Track.ControlId == TEXT("right_hand.yaw") ||
				Track.ControlId == TEXT("right_hand.roll") ||
				Track.ControlId == TEXT("left_hand.pitch") ||
				Track.ControlId == TEXT("left_hand.yaw") ||
				Track.ControlId == TEXT("left_hand.roll")
		);
	}

	const FLLMMotionTrack* RightSeparation = FindForwardN7BTrack(
		Plan.Clip,
		TEXT("right_hand.local_offset.x")
	);
	const FLLMMotionTrack* LeftSeparation = FindForwardN7BTrack(
		Plan.Clip,
		TEXT("left_hand.local_offset.x")
	);
	TestNotNull(
		TEXT("The right contact-separation track exists"),
		RightSeparation
	);
	TestNotNull(
		TEXT("The left contact-separation track exists"),
		LeftSeparation
	);
	if (!RightSeparation || !LeftSeparation)
	{
		return false;
	}
	const FLLMMotionTrack* RightIK = FindForwardN7BTrack(
		Plan.Clip,
		TEXT("right_hand.ik")
	);
	const FLLMMotionTrack* LeftIK = FindForwardN7BTrack(
		Plan.Clip,
		TEXT("left_hand.ik")
	);
	const FLLMMotionTrack* RightPalmFacing = FindForwardN7BTrack(
		Plan.Clip,
		TEXT("right_hand.palm_facing")
	);
	const FLLMMotionTrack* LeftPalmFacing = FindForwardN7BTrack(
		Plan.Clip,
		TEXT("left_hand.palm_facing")
	);
	TestNotNull(TEXT("The right Clap IK track exists"), RightIK);
	TestNotNull(TEXT("The left Clap IK track exists"), LeftIK);
	TestNotNull(
		TEXT("The right Clap palm-facing track exists"),
		RightPalmFacing
	);
	TestNotNull(
		TEXT("The left Clap palm-facing track exists"),
		LeftPalmFacing
	);
	if (!RightIK || !LeftIK || !RightPalmFacing || !LeftPalmFacing)
	{
		return false;
	}
	TestTrue(
		TEXT("Clap placement and palm orientation use a sustained envelope"),
		RightIK->Envelope == ELLMMotionEnvelope::Sustain &&
			LeftIK->Envelope == ELLMMotionEnvelope::Sustain &&
			RightPalmFacing->Envelope == ELLMMotionEnvelope::Sustain &&
			LeftPalmFacing->Envelope == ELLMMotionEnvelope::Sustain
	);
	TestEqual(
		TEXT("Mirrored separation tracks have matching keys"),
		RightSeparation->FloatKeys.Num(),
		LeftSeparation->FloatKeys.Num()
	);
	int32 ContactKeyCount = 0;
	for (
		int32 KeyIndex = 0;
		KeyIndex < RightSeparation->FloatKeys.Num();
		++KeyIndex
	)
	{
		const FLLMMotionKeyFloat& RightKey =
			RightSeparation->FloatKeys[KeyIndex];
		const FLLMMotionKeyFloat& LeftKey =
			LeftSeparation->FloatKeys[KeyIndex];
		TestTrue(
			TEXT("The right hand never crosses the contact center"),
			RightKey.V <= KINDA_SMALL_NUMBER
		);
		TestTrue(
			TEXT("The left hand never crosses the contact center"),
			LeftKey.V >= -KINDA_SMALL_NUMBER
		);
		TestTrue(
			TEXT("Contact separation remains exactly mirrored"),
			FMath::IsNearlyEqual(RightKey.V, -LeftKey.V)
		);
		const bool bBoundaryKey =
			KeyIndex == 0 ||
			KeyIndex == RightSeparation->FloatKeys.Num() - 1;
		if (!bBoundaryKey && FMath::IsNearlyZero(RightKey.V))
		{
			++ContactKeyCount;
		}
	}
	TestEqual(
		TEXT("Two cycles emit two bounded contact moments"),
		ContactKeyCount,
		2
	);
	TestTrue(
		TEXT("The lateral offsets return to neutral at both boundaries"),
		FMath::IsNearlyZero(
			RightSeparation->FloatKeys[0].V
		) &&
			FMath::IsNearlyZero(
				LeftSeparation->FloatKeys[0].V
			) &&
			FMath::IsNearlyZero(
				RightSeparation->FloatKeys.Last().V
			) &&
			FMath::IsNearlyZero(
				LeftSeparation->FloatKeys.Last().V
			)
	);

	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	FLLMProceduralPoseSnapshot ContactSnapshot;
	FLLMNPCMotionSampler::SampleClip(
		Plan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		RightSeparation->FloatKeys[2].T,
		ContactSnapshot
	);
	FLLMProceduralPoseSnapshot OpenSnapshot;
	FLLMNPCMotionSampler::SampleClip(
		Plan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		RightSeparation->FloatKeys[1].T,
		OpenSnapshot
	);
	const float ContactDistance = FVector::Distance(
		ContactSnapshot.RightHandIKTargetCS,
		ContactSnapshot.LeftHandIKTargetCS
	);
	const float OpenDistance = FVector::Distance(
		OpenSnapshot.RightHandIKTargetCS,
		OpenSnapshot.LeftHandIKTargetCS
	);
	TestTrue(
		TEXT("The calibrated contact keeps a safe non-zero palm gap"),
		ContactDistance >= 7.5f && ContactDistance <= 9.5f
	);
	TestTrue(
		TEXT("The open phase separates the hands visibly"),
		OpenDistance > ContactDistance + 10.0f
	);
	TestTrue(
		TEXT("Both palm faces target the shared contact center"),
		ContactSnapshot.RightHandPalmFacingTargetCS.Equals(
			ContactSnapshot.LeftHandPalmFacingTargetCS,
			0.01f
		) &&
			ContactSnapshot.RightHandPalmFacingAlpha > 0.5f &&
			ContactSnapshot.LeftHandPalmFacingAlpha > 0.5f
	);
	TestTrue(
		TEXT("Both wrists reach near-full orientation at every contact"),
		ContactSnapshot.RightHandIKAlpha > 0.9f &&
			ContactSnapshot.LeftHandIKAlpha > 0.9f &&
			ContactSnapshot.RightHandPalmFacingAlpha > 0.9f &&
			ContactSnapshot.LeftHandPalmFacingAlpha > 0.9f
	);
	const FVector RightPalmFacingDirection = (
		ContactSnapshot.RightHandPalmFacingTargetCS -
		ContactSnapshot.RightHandIKTargetCS
	).GetSafeNormal();
	const FVector LeftPalmFacingDirection = (
		ContactSnapshot.LeftHandPalmFacingTargetCS -
		ContactSnapshot.LeftHandIKTargetCS
	).GetSafeNormal();
	TestTrue(
		TEXT("The two palm normals face one another at contact"),
		FVector::DotProduct(
			RightPalmFacingDirection,
			LeftPalmFacingDirection
		) < -0.99f
	);
	TestTrue(
		TEXT("Clap does not reuse the finger-aim palm target"),
		ContactSnapshot.RightHandPalmAlpha <= KINDA_SMALL_NUMBER &&
			ContactSnapshot.LeftHandPalmAlpha <= KINDA_SMALL_NUMBER
	);
	TestTrue(
		TEXT("Both hands use the near-full contact pose during contact"),
		ContactSnapshot.RightFingersContact > 0.95f &&
			ContactSnapshot.LeftFingersContact > 0.95f
	);
	TestTrue(
		TEXT("Clap does not alter the reviewed generic open-hand pose"),
		ContactSnapshot.RightFingersOpen <= KINDA_SMALL_NUMBER &&
			ContactSnapshot.LeftFingersOpen <= KINDA_SMALL_NUMBER
	);
	TestTrue(
		TEXT("The Clap stays in front of the chest"),
		ContactSnapshot.RightHandIKTargetCS.Y > 0.0f &&
			ContactSnapshot.LeftHandIKTargetCS.Y > 0.0f
	);

	const FLLMAnchorDefinition* RightAnchor =
		ULLMNPCControlManifest::FindBuiltInAnchor(
			TEXT("right_clap")
		);
	const FLLMAnchorDefinition* LeftAnchor =
		ULLMNPCControlManifest::FindBuiltInAnchor(
			TEXT("left_clap")
		);
	TestNotNull(TEXT("The right Clap anchor exists"), RightAnchor);
	TestNotNull(TEXT("The left Clap anchor exists"), LeftAnchor);
	if (RightAnchor && LeftAnchor)
	{
		TestTrue(
			TEXT("The calibrated Clap anchors mirror Manny's lateral axis"),
			RightAnchor->OffsetCS.X < 0.0f &&
				LeftAnchor->OffsetCS.X > 0.0f &&
				FMath::IsNearlyEqual(
					RightAnchor->OffsetCS.X,
					-LeftAnchor->OffsetCS.X
				) &&
				FMath::IsNearlyEqual(
					RightAnchor->OffsetCS.Y,
					LeftAnchor->OffsetCS.Y
				) &&
				FMath::IsNearlyEqual(
					RightAnchor->OffsetCS.Z,
					LeftAnchor->OffsetCS.Z
				)
		);
	}

	FLLMMotionPlan SecondPlan;
	FLLMNPCCompiledRecipeMetadata SecondMetadata;
	TestTrue(
		TEXT("The same Clap Recipe compiles deterministically"),
		FLLMNPCMotionRecipeCompiler::Compile(
			Recipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			SecondPlan,
			SecondMetadata,
			Error
		)
	);
	TestEqual(
		TEXT("Equivalent Clap Recipes have identical compiled hashes"),
		Metadata.CompiledRecipeHash,
		SecondMetadata.CompiledRecipeHash
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7BClapPalmBasisTest,
	"LLMNPCActionLayer.ForwardN7B.ProceduralClap.ContactPalmBasis",
	ForwardN7BTestFlags
)

bool FLLMNPCForwardN7BClapPalmBasisTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const FVector ComponentForward = FVector::RightVector;
	const FVector ComponentUp = FVector::UpVector;
	FVector RightFinger;
	FVector RightPalm;
	FVector LeftFinger;
	FVector LeftPalm;
	TestTrue(
		TEXT("A raised right-hand contact direction resolves"),
		FLLMNPCArmIKSolver::BuildStableContactPalmBasis(
			ComponentForward,
			ComponentUp,
			FVector(4.0f, 120.0f, 90.0f),
			true,
			RightFinger,
			RightPalm
		)
	);
	TestTrue(
		TEXT("A lowered left-hand contact direction resolves"),
		FLLMNPCArmIKSolver::BuildStableContactPalmBasis(
			ComponentForward,
			ComponentUp,
			FVector(-4.0f, -120.0f, -90.0f),
			false,
			LeftFinger,
			LeftPalm
		)
	);
	TestTrue(
		TEXT("Contact palms face one another only on Manny's lateral axis"),
		RightPalm.Equals(FVector::ForwardVector, 0.001f) &&
			LeftPalm.Equals(-FVector::ForwardVector, 0.001f) &&
			FVector::DotProduct(RightPalm, LeftPalm) < -0.999f
	);
	TestTrue(
		TEXT("Both Clap finger axes point together toward forward and up"),
		RightFinger.Equals(LeftFinger, 0.001f) &&
			FVector::DotProduct(RightFinger, ComponentForward) > 0.65f &&
			FVector::DotProduct(RightFinger, ComponentUp) > 0.7f
	);
	TestTrue(
		TEXT("Neither Clap finger axis points toward the opposite hand"),
		FMath::Abs(FVector::DotProduct(RightFinger, RightPalm)) < 0.001f &&
			FMath::Abs(FVector::DotProduct(LeftFinger, LeftPalm)) < 0.001f
	);

	FVector MovingFinger;
	FVector MovingPalm;
	TestTrue(
		TEXT("A changing lift trajectory keeps a valid contact basis"),
		FLLMNPCArmIKSolver::BuildStableContactPalmBasis(
			ComponentForward,
			ComponentUp,
			FVector(4.0f, -300.0f, 500.0f),
			true,
			MovingFinger,
			MovingPalm
		)
	);
	TestTrue(
		TEXT("Forward and height changes cannot flip the right palm"),
		MovingPalm.Equals(RightPalm, 0.001f) &&
			MovingFinger.Equals(RightFinger, 0.001f)
	);

	FVector FallbackRightFinger;
	FVector FallbackRightPalm;
	FVector FallbackLeftFinger;
	FVector FallbackLeftPalm;
	TestTrue(
		TEXT("Zero-distance contact uses a stable right-side fallback"),
		FLLMNPCArmIKSolver::BuildStableContactPalmBasis(
			ComponentForward,
			ComponentUp,
			FVector::ZeroVector,
			true,
			FallbackRightFinger,
			FallbackRightPalm
		)
	);
	TestTrue(
		TEXT("Zero-distance contact uses a stable left-side fallback"),
		FLLMNPCArmIKSolver::BuildStableContactPalmBasis(
			ComponentForward,
			ComponentUp,
			FVector::ZeroVector,
			false,
			FallbackLeftFinger,
			FallbackLeftPalm
		)
	);
	TestTrue(
		TEXT("Fallback palms retain the same opposing contact orientation"),
		FallbackRightPalm.Equals(RightPalm, 0.001f) &&
			FallbackLeftPalm.Equals(LeftPalm, 0.001f) &&
			FallbackRightFinger.Equals(RightFinger, 0.001f) &&
			FallbackLeftFinger.Equals(LeftFinger, 0.001f)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN7BClapSandboxPreflightTest,
	"LLMNPCActionLayer.ForwardN7B.ProceduralClap.SandboxPreflight",
	ForwardN7BTestFlags
)

bool FLLMNPCForwardN7BClapSandboxPreflightTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* Profile = LoadN7BMannyProfile();
	TestNotNull(TEXT("Manny Profile loads"), Profile);
	if (!Profile)
	{
		return false;
	}

	FLLMNPCAuthoringSandboxRequest Request;
	Request.RecipeJson = ValidClapRecipe;
	Request.SkeletonProfile = Profile;
	const FLLMNPCAuthoringSandboxPreflightResult Result =
		FLLMNPCAuthoringSandbox::RunFullPreflight(Request);
	TestTrue(
		*FString::Printf(
			TEXT("The procedural Clap passes Full Preflight: %s"),
			*Result.ErrorMessage
		),
		Result.bPassed
	);
	TestEqual(
		TEXT("Successful Clap Preflight reaches Ready"),
		Result.Stage,
		ELLMNPCAuthoringSandboxStage::Ready
	);
	TestTrue(
		TEXT("The complete Clap Kinematic report passes"),
		Result.KinematicReport.bPassed &&
			Result.KinematicReport.ReportHash.StartsWith(TEXT("md5:"))
	);
	TestTrue(
		TEXT("Preflight produces only a transient bounded plan"),
		!Result.TransientPlan.Clip.Tracks.IsEmpty() &&
			Result.TransientPlan.Clip.Tracks.Num() == 8
	);
	return true;
}

#endif
