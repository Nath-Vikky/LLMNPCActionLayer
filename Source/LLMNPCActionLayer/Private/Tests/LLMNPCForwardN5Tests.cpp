#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "Dom/JsonObject.h"
#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionSampler.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeCompiler.h"
#include "MotionRecipe/LLMNPCMotionRecipeParser.h"
#include "MotionRecipe/LLMNPCMotionRecipeValidator.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

namespace
{
constexpr uint32 ForwardN5TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FString ValidShrugRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "generated.shrug.001",
  "intent": "express_uncertainty",
  "duration": 1.8,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "shoulder.shrug",
      "side": "none",
      "start": 0.0,
      "end": 1.8,
      "parameters": {
        "amplitude": 0.72,
        "torso_participation": 0.4
      }
    }
  ]
}
)JSON");

ULLMNPCSkeletonProfile* LoadN5MannyProfile()
{
	return LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
}

bool BuildMannyCapability(
	FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot,
	FString& OutError
)
{
	const ULLMNPCSkeletonProfile* Profile = LoadN5MannyProfile();
	if (!Profile)
	{
		OutError = TEXT("Manny Profile did not load.");
		return false;
	}
	const FLLMNPCSkeletonCapabilityBuildResult Result =
		FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
			*Profile,
			nullptr,
			FDateTime(2026, 7, 26, 9, 0, 0),
			OutSnapshot
		);
	if (!Result.bSucceeded)
	{
		OutError = FString::Join(Result.Errors, TEXT("; "));
		return false;
	}
	return true;
}

bool ParseAndValidateShrug(
	const FString& Json,
	FLLMNPCMotionRecipe& OutRecipe,
	FLLMNPCMotionRecipeValidationResult& OutValidation,
	FString& OutError
)
{
	if (!FLLMNPCMotionRecipeParser::Parse(Json, OutRecipe, OutError))
	{
		return false;
	}
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	if (!BuildMannyCapability(Capability, OutError))
	{
		return false;
	}
	FLLMNPCMotionRecipeValidationContext Context;
	return FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
		OutRecipe,
		Capability,
		FLLMNPCMotionPrimitiveRegistry::Get(),
		Context,
		OutValidation
	);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5MotionRecipeParserTest,
	"LLMNPCActionLayer.ForwardN5.MotionRecipe.StrictParser",
	ForwardN5TestFlags
)

bool FLLMNPCForwardN5MotionRecipeParserTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCMotionRecipe Recipe;
	FString Error;
	TestTrue(
		TEXT("A bounded Shrug Recipe parses"),
		FLLMNPCMotionRecipeParser::Parse(ValidShrugRecipe, Recipe, Error)
	);
	TestEqual(
		TEXT("The parser preserves the semantic primitive"),
		Recipe.Primitives[0].PrimitiveId,
		FName(TEXT("shoulder.shrug"))
	);
	TestEqual(
		TEXT("Recipe time is represented in seconds"),
		Recipe.DurationSeconds,
		1.8
	);

	const FString UnknownRoot = ValidShrugRecipe.Replace(
		TEXT("\"primitives\": ["),
		TEXT("\"root_motion\": true, \"primitives\": [")
	);
	TestFalse(
		TEXT("Unknown root fields are rejected"),
		FLLMNPCMotionRecipeParser::Parse(UnknownRoot, Recipe, Error)
	);
	TestTrue(
		TEXT("The root boundary has a stable error"),
		Error.Contains(TEXT("LLMNPC_RECIPE_ROOT_FIELD_UNKNOWN"))
	);

	const FString SolverInjection = ValidShrugRecipe.Replace(
		TEXT("\"side\": \"none\","),
		TEXT("\"side\": \"none\", \"solver_id\": \"unsafe\",")
	);
	TestFalse(
		TEXT("The model cannot submit a Solver ID"),
		FLLMNPCMotionRecipeParser::Parse(SolverInjection, Recipe, Error)
	);
	TestTrue(
		TEXT("Solver injection is an unknown primitive field"),
		Error.Contains(TEXT("LLMNPC_RECIPE_PRIMITIVE_FIELD_UNKNOWN"))
	);

	const FString TargetRefInjection = ValidShrugRecipe.Replace(
		TEXT("\"side\": \"none\","),
		TEXT("\"side\": \"none\", \"target_ref\": \"player.main\",")
	);
	TestFalse(
		TEXT("A concrete TargetRef cannot be baked into a Recipe"),
		FLLMNPCMotionRecipeParser::Parse(TargetRefInjection, Recipe, Error)
	);

	const FString InvalidSide = ValidShrugRecipe.Replace(
		TEXT("\"side\": \"none\""),
		TEXT("\"side\": \"both\"")
	);
	TestFalse(
		TEXT("Recipe v1 rejects the non-contract side value 'both'"),
		FLLMNPCMotionRecipeParser::Parse(InvalidSide, Recipe, Error)
	);
	TestEqual(
		TEXT("Invalid sides have a stable error"),
		Error,
		FString(TEXT("LLMNPC_RECIPE_SIDE_INVALID"))
	);

	const FString NestedParameter = ValidShrugRecipe.Replace(
		TEXT("\"amplitude\": 0.72"),
		TEXT("\"amplitude\": {\"bone_name\": \"clavicle_r\"}")
	);
	TestFalse(
		TEXT("Parameters cannot contain nested transform-like objects"),
		FLLMNPCMotionRecipeParser::Parse(NestedParameter, Recipe, Error)
	);
	TestTrue(
		TEXT("Nested parameter values are structurally rejected"),
		Error.Contains(TEXT("LLMNPC_RECIPE_PARAMETER_TYPE_UNSUPPORTED"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5PrimitiveRegistrySchemaTest,
	"LLMNPCActionLayer.ForwardN5.MotionRecipe.SafeModelSchema",
	ForwardN5TestFlags
)

bool FLLMNPCForwardN5PrimitiveRegistrySchemaTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability is available"),
		BuildMannyCapability(Capability, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}

	FString SchemaJson;
	TestTrue(
		TEXT("The Registry produces a model-facing JSON Schema"),
		FLLMNPCMotionPrimitiveRegistry::Get().BuildModelSchemaJson(
			&Capability,
			SchemaJson,
			Error
		)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	TSharedPtr<FJsonObject> SchemaObject;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(SchemaJson);
	TestTrue(
		TEXT("The generated JSON Schema is valid JSON"),
		FJsonSerializer::Deserialize(Reader, SchemaObject) &&
			SchemaObject.IsValid()
	);
	TestTrue(
		TEXT("The schema is tied to the Registry version"),
		SchemaJson.Contains(LLMNPCMotionRecipe::RegistryVersion)
	);
	TestTrue(
		TEXT("The schema uses a Primitive oneOf"),
		SchemaJson.Contains(TEXT("\"oneOf\""))
	);
	TestTrue(
		TEXT("Manny exposes the Authoring-only Shrug primitive"),
		SchemaJson.Contains(TEXT("shoulder.shrug"))
	);
	for (
		const TCHAR* RestrictedValue :
		{
			TEXT("solver.shoulder"),
			TEXT("clavicle_r"),
			TEXT("upperarm_r"),
			TEXT("bone_name"),
			TEXT("transform"),
			TEXT("asset_path"),
			TEXT("quaternion")
		}
	)
	{
		TestFalse(
			FString::Printf(
				TEXT("The model Schema omits internal value '%s'"),
				RestrictedValue
			),
			SchemaJson.Contains(RestrictedValue, ESearchCase::IgnoreCase)
		);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5MotionRecipeValidatorTest,
	"LLMNPCActionLayer.ForwardN5.MotionRecipe.CapabilityAndPolicy",
	ForwardN5TestFlags
)

bool FLLMNPCForwardN5MotionRecipeValidatorTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability is available"),
		BuildMannyCapability(Capability, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}

	FLLMNPCMotionRecipe Recipe;
	TestTrue(
		TEXT("The valid fixture parses"),
		FLLMNPCMotionRecipeParser::Parse(ValidShrugRecipe, Recipe, Error)
	);
	FLLMNPCMotionRecipeValidationContext Context;
	FLLMNPCMotionRecipeValidationResult Validation;
	TestTrue(
		TEXT("Manny accepts the bounded bilateral Shrug Recipe"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			Recipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("Defaults are inserted before hashing and compilation"),
		Recipe.Primitives[0].Parameters.Contains(TEXT("arm_openness")) &&
			Recipe.Primitives[0].Parameters.Contains(TEXT("palm_openness")) &&
			Recipe.Primitives[0].Parameters.Contains(TEXT("speed"))
	);
	TestTrue(
		TEXT("The Shrug reserves both arm channels"),
		Validation.RequiredChannels.Contains(TEXT("left_arm_ik")) &&
			Validation.RequiredChannels.Contains(TEXT("right_arm_ik"))
	);
	TestFalse(
		TEXT("No concrete target is embedded"),
		!Validation.UsedTargetSlots.IsEmpty()
	);

	FLLMNPCMotionRecipe UnknownParameterRecipe;
	TestTrue(
		TEXT("An unknown scalar parameter remains structurally parseable"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidShrugRecipe.Replace(
				TEXT("\"amplitude\": 0.72,"),
				TEXT("\"bone_name\": \"clavicle_r\", \"amplitude\": 0.72,")
			),
			UnknownParameterRecipe,
			Error
		)
	);
	TestFalse(
		TEXT("The Registry rejects a raw bone parameter"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			UnknownParameterRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The raw bone parameter has a stable error"),
		Validation.ErrorCode.Contains(TEXT("LLMNPC_RECIPE_PARAMETER_UNKNOWN"))
	);

	FLLMNPCMotionRecipe OutOfRangeRecipe;
	TestTrue(
		TEXT("An out-of-range number remains structurally parseable"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidShrugRecipe.Replace(
				TEXT("\"amplitude\": 0.72"),
				TEXT("\"amplitude\": 1.25")
			),
			OutOfRangeRecipe,
			Error
		)
	);
	TestFalse(
		TEXT("Out-of-range semantic parameters fail closed"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			OutOfRangeRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The range failure identifies the parameter"),
		Validation.ErrorCode.Contains(TEXT("PARAMETER_OUT_OF_RANGE:amplitude"))
	);

	FLLMNPCMotionRecipe MissingCapabilityRecipe;
	TestTrue(
		TEXT("The valid fixture can be reparsed"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidShrugRecipe,
			MissingCapabilityRecipe,
			Error
		)
	);
	Capability.Capabilities.RemoveAll(
		[](const FLLMNPCSemanticCapability& Candidate)
		{
			return Candidate.CapabilityId == TEXT("shoulder.shrug");
		}
	);
	TestFalse(
		TEXT("A Skeleton without the semantic capability cannot compile it"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			MissingCapabilityRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("Missing capability failure is explicit"),
		Validation.ErrorCode.Contains(TEXT("CAPABILITY_MISSING"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5MotionRecipeDeterminismTest,
	"LLMNPCActionLayer.ForwardN5.MotionRecipe.DeterminismAndConflicts",
	ForwardN5TestFlags
)

bool FLLMNPCForwardN5MotionRecipeDeterminismTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCMotionRecipe First;
	FLLMNPCMotionRecipeValidationResult FirstValidation;
	FString Error;
	TestTrue(
		TEXT("The first Recipe validates"),
		ParseAndValidateShrug(
			ValidShrugRecipe,
			First,
			FirstValidation,
			Error
		)
	);
	FLLMNPCMotionRecipe Second;
	FLLMNPCMotionRecipeValidationResult SecondValidation;
	TestTrue(
		TEXT("The second Recipe validates"),
		ParseAndValidateShrug(
			ValidShrugRecipe,
			Second,
			SecondValidation,
			Error
		)
	);
	FString FirstCanonical;
	FString SecondCanonical;
	TestTrue(
		TEXT("The first normalized Recipe canonicalizes"),
		FLLMNPCMotionRecipeCanonicalizer::BuildCanonicalJson(
			First,
			FirstCanonical,
			Error
		)
	);
	TestTrue(
		TEXT("The second normalized Recipe canonicalizes"),
		FLLMNPCMotionRecipeCanonicalizer::BuildCanonicalJson(
			Second,
			SecondCanonical,
			Error
		)
	);
	TestEqual(
		TEXT("Normalization and canonical ordering are deterministic"),
		FirstCanonical,
		SecondCanonical
	);
	TestEqual(
		TEXT("Equivalent canonical Recipes have the same hash"),
		FLLMNPCMotionRecipeCanonicalizer::BuildRecipeHash(FirstCanonical),
		FLLMNPCMotionRecipeCanonicalizer::BuildRecipeHash(SecondCanonical)
	);

	const FString ConflictingHeadRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "generated.head.conflict",
  "intent": "conflicting_head_motion",
  "duration": 1.0,
  "interruptible": true,
  "primitives": [
    {
      "primitive_id": "head.nod",
      "start": 0.0,
      "end": 1.0,
      "parameters": {}
    },
    {
      "primitive_id": "head.shake",
      "start": 0.2,
      "end": 0.8,
      "parameters": {}
    }
  ]
}
)JSON");
	FLLMNPCMotionRecipe Conflict;
	TestTrue(
		TEXT("A structurally valid overlapping Recipe parses"),
		FLLMNPCMotionRecipeParser::Parse(
			ConflictingHeadRecipe,
			Conflict,
			Error
		)
	);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	TestTrue(
		TEXT("The Manny Capability is available"),
		BuildMannyCapability(Capability, Error)
	);
	FLLMNPCMotionRecipeValidationContext Context;
	FLLMNPCMotionRecipeValidationResult Validation;
	TestFalse(
		TEXT("Exclusive head-channel overlap is rejected"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			Conflict,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestTrue(
		TEXT("The conflict names the occupied semantic channel"),
		Validation.ErrorCode.Contains(TEXT("CHANNEL_CONFLICT:head"))
	);

	FLLMNPCMotionRecipe RuntimeRecipe;
	TestTrue(
		TEXT("The valid fixture reparses for the runtime boundary"),
		FLLMNPCMotionRecipeParser::Parse(
			ValidShrugRecipe,
			RuntimeRecipe,
			Error
		)
	);
	Context.Mode = ELLMNPCMotionRecipeMode::PublishedRuntime;
	TestFalse(
		TEXT("Published Runtime never accepts a new Recipe"),
		FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
			RuntimeRecipe,
			Capability,
			FLLMNPCMotionPrimitiveRegistry::Get(),
			Context,
			Validation
		)
	);
	TestEqual(
		TEXT("The runtime boundary has a stable reason"),
		Validation.ErrorCode,
		FString(TEXT("LLMNPC_RECIPE_RUNTIME_DISABLED"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5MotionRecipeCompilerTest,
	"LLMNPCActionLayer.ForwardN5.MotionRecipe.ShrugCompiler",
	ForwardN5TestFlags
)

bool FLLMNPCForwardN5MotionRecipeCompilerTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability is available"),
		BuildMannyCapability(Capability, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}
	FLLMNPCMotionRecipe Recipe;
	TestTrue(
		TEXT("The Shrug fixture parses"),
		FLLMNPCMotionRecipeParser::Parse(ValidShrugRecipe, Recipe, Error)
	);
	FLLMNPCMotionRecipeCompileContext Context;
	FLLMMotionPlan Plan;
	FLLMNPCCompiledRecipeMetadata Metadata;
	TestTrue(
		TEXT("The semantic Shrug compiles to a bounded MotionPlan"),
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
	TestTrue(
		TEXT("Compilation records deterministic content hashes"),
		Metadata.RecipeHash.StartsWith(TEXT("md5:")) &&
			Metadata.CompiledRecipeHash.StartsWith(TEXT("md5:"))
	);
	TestEqual(
		TEXT("Compilation records the exact Manny Capability"),
		Metadata.CapabilityHash,
		Capability.CapabilityHash
	);
	TestEqual(
		TEXT("Compilation records the corrected Shrug Solver version"),
		Metadata.CompilerVersion,
		FString(TEXT("llmnpc.motion_recipe_compiler.v4"))
	);
	TestEqual(
		TEXT("One semantic primitive has one mapping record"),
		Metadata.PrimitiveMappings.Num(),
		1
	);
	TestEqual(
		TEXT("The Registry chose the internal Shrug Solver"),
		Metadata.PrimitiveMappings[0].SolverId,
		FName(TEXT("solver.shoulder_shrug.manny.v1"))
	);
	TestTrue(
		TEXT("The full Shrug Solver drives both shoulders"),
		Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
			TEXT("right_shoulder.pitch")
		) &&
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("left_shoulder.pitch")
			) &&
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("right_shoulder.yaw")
			) &&
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("left_shoulder.yaw")
			)
	);
	TestTrue(
		TEXT("The full Shrug Solver includes chest, arms, and relaxed hands"),
		Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
			TEXT("chest.pitch")
		) &&
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("right_hand.ik")
			) &&
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("left_hand.ik")
			) &&
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("right_fingers.relaxed")
			) &&
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("left_fingers.relaxed")
			)
	);
	TestFalse(
		TEXT("The Shrug Solver does not inject skeleton-local wrist Euler angles"),
		Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
			TEXT("right_hand.roll")
		) ||
			Metadata.PrimitiveMappings[0].GeneratedControlIds.Contains(
				TEXT("left_hand.roll")
			)
	);

	FLLMProceduralPoseSnapshot Snapshot;
	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	FLLMNPCMotionSampler::SampleClip(
		Plan.Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		Plan.Clip.Duration * 0.5f,
		Snapshot
	);
	TestTrue(
		TEXT("The right shoulder lifts with the calibrated sign"),
		Snapshot.RightShoulderAdditiveRotation.Pitch < -8.0f
	);
	TestTrue(
		TEXT("The left shoulder lifts with the mirrored sign"),
		Snapshot.LeftShoulderAdditiveRotation.Pitch > 8.0f
	);
	TestTrue(
		TEXT("Shoulder protraction uses mirrored component-space signs"),
		Snapshot.RightShoulderAdditiveRotation.Yaw < 0.0f &&
			Snapshot.LeftShoulderAdditiveRotation.Yaw > 0.0f
	);
	TestTrue(
		TEXT("The corrected Shrug does not twist either clavicle around its length"),
		FMath::IsNearlyZero(
			Snapshot.RightShoulderAdditiveRotation.Roll
		) &&
			FMath::IsNearlyZero(
				Snapshot.LeftShoulderAdditiveRotation.Roll
			)
	);
	TestTrue(
		TEXT("Both arm anchors participate"),
		Snapshot.RightHandIKAlpha > 0.5f &&
			Snapshot.LeftHandIKAlpha > 0.5f
	);
	TestTrue(
		TEXT("Both hands use the relaxed calibrated pose"),
		Snapshot.RightFingersRelaxed > 0.5f &&
			Snapshot.LeftFingersRelaxed > 0.5f
	);
	TestTrue(
		TEXT("Hand presentation is geometry-driven instead of local Euler-driven"),
		Snapshot.RightHandAdditiveRotation.IsNearlyZero() &&
			Snapshot.LeftHandAdditiveRotation.IsNearlyZero()
	);
	TestTrue(
		TEXT("The right and left hand targets stay on their own Manny side"),
		Snapshot.RightHandIKTargetCS.X < 0.0f &&
			Snapshot.LeftHandIKTargetCS.X > 0.0f
	);
	TestTrue(
		TEXT("The hand targets are mirrored across Manny's component X axis"),
		FMath::IsNearlyEqual(
			Snapshot.RightHandIKTargetCS.X,
			-Snapshot.LeftHandIKTargetCS.X
		) &&
			FMath::IsNearlyEqual(
				Snapshot.RightHandIKTargetCS.Y,
				Snapshot.LeftHandIKTargetCS.Y
			) &&
			FMath::IsNearlyEqual(
				Snapshot.RightHandIKTargetCS.Z,
				Snapshot.LeftHandIKTargetCS.Z
			)
	);
	TestTrue(
		TEXT("Both Shrug hands remain in front of the chest"),
		Snapshot.RightHandIKTargetCS.Y > 0.0f &&
			Snapshot.LeftHandIKTargetCS.Y > 0.0f
	);

	const FLLMAnchorDefinition* RightShrugAnchor =
		ULLMNPCControlManifest::FindBuiltInAnchor(TEXT("right_shrug"));
	const FLLMAnchorDefinition* LeftShrugAnchor =
		ULLMNPCControlManifest::FindBuiltInAnchor(TEXT("left_shrug"));
	TestNotNull(TEXT("The right Shrug anchor exists"), RightShrugAnchor);
	TestNotNull(TEXT("The left Shrug anchor exists"), LeftShrugAnchor);
	if (RightShrugAnchor && LeftShrugAnchor)
	{
		TestTrue(
			TEXT("Built-in Shrug anchors mirror only the Manny lateral axis"),
			RightShrugAnchor->OffsetCS.X < 0.0f &&
				LeftShrugAnchor->OffsetCS.X > 0.0f &&
				FMath::IsNearlyEqual(
					RightShrugAnchor->OffsetCS.X,
					-LeftShrugAnchor->OffsetCS.X
				) &&
				FMath::IsNearlyEqual(
					RightShrugAnchor->OffsetCS.Y,
					LeftShrugAnchor->OffsetCS.Y
				) &&
				FMath::IsNearlyEqual(
					RightShrugAnchor->OffsetCS.Z,
					LeftShrugAnchor->OffsetCS.Z
				)
		);
	}

	FLLMMotionPlan SecondPlan;
	FLLMNPCCompiledRecipeMetadata SecondMetadata;
	TestTrue(
		TEXT("The same Recipe compiles a second time"),
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
		TEXT("Compiled Recipe hashes are deterministic"),
		Metadata.CompiledRecipeHash,
		SecondMetadata.CompiledRecipeHash
	);
	TestEqual(
		TEXT("Deterministic compilation produces the same track count"),
		Plan.Clip.Tracks.Num(),
		SecondPlan.Clip.Tracks.Num()
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN5MotionRecipeSolverCoverageTest,
	"LLMNPCActionLayer.ForwardN5.MotionRecipe.RegistrySolverCoverage",
	ForwardN5TestFlags
)

bool FLLMNPCForwardN5MotionRecipeSolverCoverageTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	FLLMNPCSkeletonCapabilitySnapshot Capability;
	FString Error;
	TestTrue(
		TEXT("The Manny Capability is available"),
		BuildMannyCapability(Capability, Error)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
		return false;
	}

	const FLLMNPCMotionPrimitiveRegistry& Registry =
		FLLMNPCMotionPrimitiveRegistry::Get();
	for (
		const FLLMNPCMotionPrimitiveDefinition& Definition :
		Registry.GetDefinitions()
	)
	{
		if (
			Definition.Availability ==
				ELLMNPCMotionPrimitiveAvailability::Disabled
		)
		{
			continue;
		}
		FLLMNPCMotionRecipe Recipe;
		Recipe.RecipeId = FString::Printf(
			TEXT("coverage.%s"),
			*Definition.PrimitiveId.ToString()
		);
		Recipe.Intent = TEXT("solver_coverage");
		Recipe.DurationSeconds = 1.2;
		FLLMNPCMotionRecipePrimitive& Primitive =
			Recipe.Primitives.AddDefaulted_GetRef();
		Primitive.PrimitiveId = Definition.PrimitiveId;
		Primitive.Side = Definition.AllowedSides[0];
		Primitive.StartTimeSeconds = 0.0;
		Primitive.EndTimeSeconds = 1.2;
		if (Definition.bTargetRequired)
		{
			Primitive.TargetSlot = TEXT("primary");
		}

		FLLMNPCMotionRecipeCompileContext Context;
		Context.ValidationContext.AllowedTargetSlots.Add(TEXT("primary"));
		Context.TargetBindings.Add(TEXT("primary"), TEXT("player.main"));
		FLLMMotionPlan Plan;
		FLLMNPCCompiledRecipeMetadata Metadata;
		const bool bCompiled = FLLMNPCMotionRecipeCompiler::Compile(
			Recipe,
			Capability,
			Registry,
			Context,
			Plan,
			Metadata,
			Error
		);
		TestTrue(
			FString::Printf(
				TEXT("Registry primitive '%s' has an implemented bounded Solver"),
				*Definition.PrimitiveId.ToString()
			),
			bCompiled
		);
		if (!bCompiled)
		{
			AddError(FString::Printf(
				TEXT("%s: %s"),
				*Definition.PrimitiveId.ToString(),
				*Error
			));
		}
		else
		{
			TestTrue(
				FString::Printf(
					TEXT("Registry primitive '%s' emits tracks"),
					*Definition.PrimitiveId.ToString()
				),
				!Plan.Clip.Tracks.IsEmpty()
			);
			TestEqual(
				FString::Printf(
					TEXT("Registry primitive '%s' records one mapping"),
					*Definition.PrimitiveId.ToString()
				),
				Metadata.PrimitiveMappings.Num(),
				1
			);
		}
		Error.Reset();
	}
	return true;
}

#endif
