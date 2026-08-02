#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionSampler.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeCompiler.h"
#include "MotionRecipe/LLMNPCMotionRecipeParser.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"

namespace
{
constexpr uint32 ForwardN7DTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FString ValidPresentRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "generated.present.procedural.001",
  "intent": "indicate",
  "duration": 1.6,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "arm.present",
      "side": "right",
      "start": 0.0,
      "end": 1.6,
      "target_slot": "primary",
      "parameters": {
        "amplitude": 0.65,
        "height": 0.55
      }
    }
  ]
}
)JSON");

ULLMNPCSkeletonProfile* LoadForwardN7DMannyProfile()
{
	return LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
}

bool BuildForwardN7DCapability(
	FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot,
	FString& OutError
)
{
	const ULLMNPCSkeletonProfile* Profile =
		LoadForwardN7DMannyProfile();
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

const FLLMMotionTrack* FindForwardN7DTrack(
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

bool CompileForwardN7DPresent(
	FLLMMotionPlan& OutPlan,
	FLLMNPCCompiledRecipeMetadata& OutMetadata,
	FString& OutError
)
{
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	if (!BuildForwardN7DCapability(Capability, OutError))
	{
		return false;
	}
	FLLMNPCMotionRecipe Recipe;
	if (!FLLMNPCMotionRecipeParser::Parse(
		ValidPresentRecipe,
		Recipe,
		OutError
	))
	{
		return false;
	}
	FLLMNPCMotionRecipeCompileContext Context;
	Context.ValidationContext.AllowedTargetSlots.Add(TEXT("primary"));
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
	FLLMNPCForwardN7DPresentCapabilityTest,
	"LLMNPCActionLayer.ForwardN7D.ProceduralPresent.CapabilityAndSchema",
	ForwardN7DTestFlags
)

bool FLLMNPCForwardN7DPresentCapabilityTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability builds"),
		BuildForwardN7DCapability(Capability, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}

	const FLLMNPCSemanticCapability* Present =
		Capability.Capabilities.FindByPredicate(
			[](const FLLMNPCSemanticCapability& Candidate)
			{
				return Candidate.CapabilityId == TEXT("arm.present");
			}
		);
	TestNotNull(TEXT("Manny exposes arm.present"), Present);
	if (Present)
	{
		TestTrue(
			TEXT("Present is an Authoring-only capability"),
			Present->bAuthoringOnly &&
				!Present->bRuntimeRecipeAllowed
		);
		TestTrue(
			TEXT("Present requires a calibrated open hand"),
			Present->Requires.Contains(TEXT("hand.pose.open"))
		);
		TestTrue(
			TEXT("Present supports both hands and a scene target"),
			Present->SupportedSides.Contains(TEXT("right")) &&
				Present->SupportedSides.Contains(TEXT("left")) &&
				Present->TargetModes.Contains(TEXT("scene_target"))
		);
		TestTrue(
			TEXT("Present exposes only semantic occupancy conflicts"),
			Present->ConflictsWith.Contains(TEXT("left_hand_busy")) &&
				Present->ConflictsWith.Contains(TEXT("right_hand_busy")) &&
				Present->ConflictsWith.Contains(TEXT("two_hand_interaction"))
		);
		TestTrue(
			TEXT("Present owns the private palm-up controls internally"),
			Present->InternalControlIds.Contains(TEXT("right_hand.palm_up")) &&
				Present->InternalControlIds.Contains(TEXT("left_hand.palm_up"))
		);
	}

	TestEqual(
		TEXT("The built-in control manifest is versioned for palm-up support"),
		ULLMNPCControlManifest::GetBuiltInManifestVersion(),
		FString(TEXT("llmnpc.control_manifest.v4"))
	);
	const FLLMControlDefinition* PalmUp =
		ULLMNPCControlManifest::FindBuiltInControl(
			TEXT("right_hand.palm_up")
		);
	TestNotNull(TEXT("The right palm-up control exists"), PalmUp);
	if (PalmUp)
	{
		TestTrue(
			TEXT("Palm-up accepts only bounded pose curves"),
			PalmUp->SolverType ==
				ELLMControlSolverType::FingerPoseBlend &&
				PalmUp->MinValue == 0.0f &&
				PalmUp->MaxValue == 1.0f &&
				PalmUp->AllowedTrackTypes.Contains(
					ELLMMotionTrackType::Keyframes
				)
		);
	}

	const FLLMNPCMotionPrimitiveDefinition* Definition =
		FLLMNPCMotionPrimitiveRegistry::Get().Find(TEXT("arm.present"));
	TestNotNull(TEXT("The Registry contains arm.present"), Definition);
	if (Definition)
	{
		TestEqual(
			TEXT("Present uses the constrained Manny Solver"),
			Definition->SolverId,
			FName(TEXT("solver.arm_present.manny.v2"))
		);
		TestTrue(
			TEXT("Present requires one semantic target"),
			Definition->bTargetRequired &&
				Definition->AllowedTargetModes.Contains(TEXT("target_slot"))
		);
		TestFalse(
			TEXT("Present exposes no ignored speed parameter"),
			Definition->ParameterSchemas.ContainsByPredicate(
				[](const FLLMNPCMotionPrimitiveParameterSchema& Parameter)
				{
					return Parameter.ParameterId == TEXT("speed");
				}
			)
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
		TEXT("The model sees arm.present and its semantic target"),
		SchemaJson.Contains(TEXT("arm.present")) &&
			SchemaJson.Contains(TEXT("target_slot"))
	);
	for (const TCHAR* RestrictedValue : {
		TEXT("right_hand.palm_up"),
		TEXT("hand_r"),
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
	FLLMNPCForwardN7DPresentCompilerTest,
	"LLMNPCActionLayer.ForwardN7D.ProceduralPresent.CompilerTargetPalmAndMirror",
	ForwardN7DTestFlags
)

bool FLLMNPCForwardN7DPresentCompilerTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMMotionPlan Plan;
	FLLMNPCCompiledRecipeMetadata Metadata;
	FString Error;
	TestTrue(
		*FString::Printf(TEXT("The Present Recipe compiles: %s"), *Error),
		CompileForwardN7DPresent(Plan, Metadata, Error)
	);
	if (!Error.IsEmpty() || Plan.Clip.Tracks.IsEmpty())
	{
		AddError(Error);
		return false;
	}
	TestEqual(
		TEXT("Present emits four bounded controls"),
		Metadata.PrimitiveMappings[0].GeneratedControlIds.Num(),
		4
	);
	TestEqual(
		TEXT("The compiled target binding is deterministic"),
		Metadata.DynamicTargetBindings.FindRef(TEXT("primary")),
		FString(TEXT("semantic_primary"))
	);

	const FLLMMotionTrack* IK =
		FindForwardN7DTrack(Plan.Clip, TEXT("right_hand.ik"));
	const FLLMMotionTrack* PalmTarget =
		FindForwardN7DTrack(Plan.Clip, TEXT("right_hand.palm_target"));
	const FLLMMotionTrack* PalmUp =
		FindForwardN7DTrack(Plan.Clip, TEXT("right_hand.palm_up"));
	const FLLMMotionTrack* Open =
		FindForwardN7DTrack(Plan.Clip, TEXT("right_fingers.open"));
	TestNotNull(TEXT("Present IK exists"), IK);
	TestNotNull(TEXT("Present palm target exists"), PalmTarget);
	TestNotNull(TEXT("Present palm-up curve exists"), PalmUp);
	TestNotNull(TEXT("Present open-finger curve exists"), Open);
	if (!IK || !PalmTarget || !PalmUp || !Open)
	{
		return false;
	}
	TestTrue(
		TEXT("Present keeps a bent elbow and a right-side outward bias"),
		IK->Reach >= 0.36f &&
			IK->Reach <= 0.68f &&
			IK->Offset.Y > 0.0f
	);
	TestTrue(
		TEXT("Present uses target reach and target-facing palm guidance"),
		IK->TrackType == ELLMMotionTrackType::IKReach &&
			PalmTarget->TrackType == ELLMMotionTrackType::LookAt
	);
	TestTrue(
		TEXT("Palm-up and open fingers are normalized pose curves"),
		PalmUp->TrackType == ELLMMotionTrackType::Keyframes &&
			Open->TrackType == ELLMMotionTrackType::Keyframes &&
			PalmUp->FloatKeys.Num() >= 4 &&
			Open->FloatKeys.Num() >= 4
	);
	for (const FLLMMotionTrack& Track : Plan.Clip.Tracks)
	{
		TestEqual(
			TEXT("Every Present track uses the semantic target placeholder"),
			Track.TargetRef,
			FString(TEXT("semantic_primary"))
		);
		TestFalse(
			TEXT("Right-hand authoring emits no left-side control"),
			Track.ControlId.ToString().StartsWith(TEXT("left_"))
		);
	}

	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	TMap<FString, FLLMNPCTargetRuntimeSample> TargetSamples;
	FLLMNPCTargetRuntimeSample& Target =
		TargetSamples.Add(TEXT("semantic_primary"));
	Target.bValid = true;
	Target.Alpha = 1.0f;
	Target.LocationWS = FVector(100.0f, 0.0f, 80.0f);
	FLLMProceduralPoseSnapshot Snapshot;
	FLLMNPCMotionSampler::SampleClip(
		Plan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		0.8f,
		Snapshot,
		&TargetSamples
	);
	TestTrue(
		TEXT("The Sampler exposes both open fingers and the palm-up request"),
		Snapshot.RightFingersOpen > 0.5f &&
			Snapshot.RightHandPalmUp > 0.5f
	);
	Target.Alpha = 0.25f;
	FLLMProceduralPoseSnapshot FadingSnapshot;
	FLLMNPCMotionSampler::SampleClip(
		Plan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		0.8f,
		FadingSnapshot,
		&TargetSamples
	);
	TestTrue(
		TEXT("Target loss fades the visible palm-up and open-finger pose"),
		FMath::IsNearlyEqual(
			FadingSnapshot.RightFingersOpen,
			Snapshot.RightFingersOpen * 0.25f,
			0.001f
		) &&
			FMath::IsNearlyEqual(
				FadingSnapshot.RightHandPalmUp,
				Snapshot.RightHandPalmUp * 0.25f,
				0.001f
		)
	);

	ULLMNPCSkeletonProfile* Profile = LoadForwardN7DMannyProfile();
	ULLMNPCMotionTemplate* PointTemplate =
		LoadObject<ULLMNPCMotionTemplate>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/MT_Point_Target_Manny_v1.MT_Point_Target_Manny_v1")
		);
	TestNotNull(TEXT("The Manny Profile remains available"), Profile);
	TestNotNull(TEXT("The target template baseline remains available"), PointTemplate);
	if (!Profile || !PointTemplate)
	{
		return false;
	}
	ULLMNPCMotionTemplate* PresentTemplate =
		DuplicateObject<ULLMNPCMotionTemplate>(
			PointTemplate,
			GetTransientPackage()
		);
	PresentTemplate->ProceduralClip = Plan.Clip;
	PresentTemplate->ModifierPolicy.bAllowMirror = true;
	PresentTemplate->Metadata.RequiredCapabilities = {
		TEXT("arm.present"),
		TEXT("hand.pose.open")
	};
	PresentTemplate->Metadata.RequiredChannels = {
		TEXT("right_arm_ik"),
		TEXT("right_hand_pose")
	};
	PresentTemplate->Metadata.CatalogContentHash.Reset();
	PresentTemplate->Metadata.CatalogContentHash =
		ULLMNPCMotionTemplate::BuildCatalogContentHash(*PresentTemplate);
	FLLMNPCTemplateModifiers Modifiers;
	Modifiers.TargetRef = TEXT("runtime.actor");
	Modifiers.bMirror = true;
	FLLMMotionPlan MirroredPlan;
	Error.Reset();
	TestTrue(
		*FString::Printf(
			TEXT("Present mirrors and binds the runtime Actor: %s"),
			*Error
		),
		FLLMNPCTemplateCompiler::Compile(
			*PresentTemplate,
			Modifiers,
			*Profile,
			MirroredPlan,
			Error
		)
	);
	const FLLMMotionTrack* LeftIK =
		FindForwardN7DTrack(MirroredPlan.Clip, TEXT("left_hand.ik"));
	TestNotNull(TEXT("Right IK mirrors to left IK"), LeftIK);
	TestNotNull(
		TEXT("Right palm-up mirrors to left palm-up"),
		FindForwardN7DTrack(
			MirroredPlan.Clip,
			TEXT("left_hand.palm_up")
		)
	);
	TestNull(
		TEXT("The mirrored plan contains no right palm-up control"),
		FindForwardN7DTrack(
			MirroredPlan.Clip,
			TEXT("right_hand.palm_up")
		)
	);
	if (LeftIK)
	{
		TestTrue(
			TEXT("The mirrored elbow bias moves to the left side"),
			LeftIK->Offset.Y < 0.0f
		);
	}
	for (const FLLMMotionTrack& Track : MirroredPlan.Clip.Tracks)
	{
		TestEqual(
			TEXT("Every mirrored track uses the runtime Actor ref"),
			Track.TargetRef,
			FString(TEXT("runtime.actor"))
		);
	}
	return true;
}

#endif
