#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Sandbox/LLMNPCAuthoringSandbox.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

namespace
{
constexpr uint32 ForwardN6TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const TCHAR* ForwardN6MannyProfilePath =
	TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1");

const FString ForwardN6ValidRecipe = TEXT(R"JSON(
{
  "schema_version": "llmnpc.motion_recipe.v1",
  "recipe_id": "sandbox.shrug.automation",
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
        "speed": 1.0,
        "torso_participation": 0.35,
        "arm_openness": 0.58,
        "palm_openness": 0.76,
        "asymmetry": 0.04
      }
    }
  ]
}
)JSON");

ULLMNPCSkeletonProfile* LoadForwardN6MannyProfile()
{
	return LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		ForwardN6MannyProfilePath
	);
}

FLLMNPCAuthoringSandboxPreflightResult RunForwardN6Preflight(
	const FString& RecipeJson,
	const ULLMNPCSkeletonProfile* Profile
)
{
	FLLMNPCAuthoringSandboxRequest Request;
	Request.RecipeJson = RecipeJson;
	Request.SkeletonProfile = Profile;
	return FLLMNPCAuthoringSandbox::RunFullPreflight(Request);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN6FullPreflightTest,
	"LLMNPCActionLayer.ForwardN6.Sandbox.FullPreflight",
	ForwardN6TestFlags
)

bool FLLMNPCForwardN6FullPreflightTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* Profile =
		LoadForwardN6MannyProfile();
	TestNotNull(TEXT("Manny Profile loads"), Profile);
	if (!Profile)
	{
		return false;
	}

	const FLLMNPCAuthoringSandboxPreflightResult Result =
		RunForwardN6Preflight(ForwardN6ValidRecipe, Profile);
	TestTrue(
		*FString::Printf(
			TEXT("A bounded online Recipe passes Full Preflight: %s"),
			*Result.ErrorMessage
		),
		Result.bPassed
	);
	TestEqual(
		TEXT("Successful Preflight reaches Ready"),
		Result.Stage,
		ELLMNPCAuthoringSandboxStage::Ready
	);
	TestTrue(
		TEXT("The normalized Recipe is preserved only as semantic JSON"),
		Result.CanonicalRecipeJson.Contains(TEXT("shoulder.shrug")) &&
			!Result.CanonicalRecipeJson.Contains(TEXT("clavicle_r"))
	);
	TestTrue(
		TEXT("Preflight binds stable Recipe and compiled hashes"),
		Result.CompiledMetadata.RecipeHash.StartsWith(TEXT("md5:")) &&
			Result.CompiledMetadata.CompiledRecipeHash.StartsWith(
				TEXT("md5:")
			)
	);
	TestTrue(
		TEXT("The complete Kinematic report passes"),
		Result.KinematicReport.bPassed &&
			Result.KinematicReport.ReportHash.StartsWith(TEXT("md5:"))
	);
	TestTrue(
		TEXT("Only a transient Motion Plan is produced"),
		!Result.TransientPlan.Clip.ClipId.IsEmpty() &&
			!Result.TransientPlan.Clip.Tracks.IsEmpty()
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN6FailClosedTest,
	"LLMNPCActionLayer.ForwardN6.Sandbox.FailClosed",
	ForwardN6TestFlags
)

bool FLLMNPCForwardN6FailClosedTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* Profile =
		LoadForwardN6MannyProfile();
	TestNotNull(TEXT("Manny Profile loads"), Profile);
	if (!Profile)
	{
		return false;
	}

	const FLLMNPCAuthoringSandboxPreflightResult BadJson =
		RunForwardN6Preflight(TEXT("{not-json"), Profile);
	TestFalse(TEXT("Bad JSON is rejected"), BadJson.bPassed);
	TestTrue(
		TEXT("Bad JSON never yields an executable plan"),
		BadJson.TransientPlan.Clip.Tracks.IsEmpty()
	);
	TestTrue(
		TEXT("Bad JSON exposes a stable parser error"),
		BadJson.ErrorMessage.Contains(TEXT("LLMNPC_RECIPE_JSON_INVALID"))
	);

	const FString OutOfRangeRecipe =
		ForwardN6ValidRecipe.Replace(
			TEXT("\"amplitude\": 0.72"),
			TEXT("\"amplitude\": 4.0")
		);
	const FLLMNPCAuthoringSandboxPreflightResult OutOfRange =
		RunForwardN6Preflight(OutOfRangeRecipe, Profile);
	TestFalse(
		TEXT("Out-of-range semantic input is rejected"),
		OutOfRange.bPassed
	);
	TestTrue(
		TEXT("Rejected semantic input never yields an executable plan"),
		OutOfRange.TransientPlan.Clip.Tracks.IsEmpty()
	);
	TestTrue(
		TEXT("The range failure remains attributable"),
		OutOfRange.ErrorMessage.Contains(
			TEXT("PARAMETER_OUT_OF_RANGE")
		)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN6KinematicGateTest,
	"LLMNPCActionLayer.ForwardN6.Sandbox.KinematicGate",
	ForwardN6TestFlags
)

bool FLLMNPCForwardN6KinematicGateTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	ULLMNPCSkeletonProfile* Source =
		LoadForwardN6MannyProfile();
	TestNotNull(TEXT("Manny Profile loads"), Source);
	if (!Source)
	{
		return false;
	}
	ULLMNPCSkeletonProfile* StrictProfile =
		DuplicateObject<ULLMNPCSkeletonProfile>(
			Source,
			GetTransientPackage()
		);
	TestNotNull(TEXT("A transient Profile can be calibrated"), StrictProfile);
	if (!StrictProfile)
	{
		return false;
	}
	StrictProfile->UpperBodyConstraints.bKinematicBaselineApproved =
		true;
	for (
		FLLMNPCKinematicControlConstraint& Constraint :
		StrictProfile->ControlConstraints
	)
	{
		Constraint.MaxAngularSpeedDegreesPerSecond = 0.01f;
		Constraint.MaxAngularAccelerationDegreesPerSecondSquared =
			0.01f;
		Constraint.MaxAngularJerkDegreesPerSecondCubed = 0.01f;
		Constraint.MaxPositionSpeedCentimetersPerSecond = 0.01f;
		Constraint.MaxPositionAccelerationCentimetersPerSecondSquared =
			0.01f;
		Constraint.MaxPositionJerkCentimetersPerSecondCubed = 0.01f;
		Constraint.MaxNormalizedSpeedPerSecond = 0.01f;
		Constraint.MaxNormalizedAccelerationPerSecondSquared = 0.01f;
		Constraint.MaxNormalizedJerkPerSecondCubed = 0.01f;
	}

	const FLLMNPCAuthoringSandboxPreflightResult Result =
		RunForwardN6Preflight(
			ForwardN6ValidRecipe,
			StrictProfile
		);
	TestFalse(
		TEXT("A kinematically unsafe plan is rejected"),
		Result.bPassed
	);
	TestEqual(
		TEXT("The Full Preflight gate has a stable failure"),
		Result.ErrorCode,
		FName(
			TEXT("LLMNPC_AUTHORING_SANDBOX_KINEMATIC_PREFLIGHT_FAILED")
		)
	);
	TestTrue(
		TEXT("The rejected report retains attributable issues"),
		!Result.KinematicReport.Issues.IsEmpty()
	);
	TestTrue(
		TEXT("A Kinematic failure never exposes the candidate plan"),
		Result.TransientPlan.Clip.Tracks.IsEmpty()
	);
	return true;
}

#endif
