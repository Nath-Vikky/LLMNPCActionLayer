#include "Sandbox/LLMNPCAuthoringSandbox.h"

#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "LLMNPCControlManifest.h"
#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"
#include "MotionRecipe/LLMNPCMotionRecipeCompiler.h"
#include "MotionRecipe/LLMNPCMotionRecipeParser.h"
#include "MotionRecipe/LLMNPCMotionRecipeValidator.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

namespace
{
FLLMNPCAuthoringSandboxPreflightResult Reject(
	ELLMNPCAuthoringSandboxStage Stage,
	const FString& Error
)
{
	static_cast<void>(Stage);
	FLLMNPCAuthoringSandboxPreflightResult Result;
	Result.Stage = ELLMNPCAuthoringSandboxStage::Rejected;
	Result.ErrorMessage = Error.IsEmpty()
		? TEXT("LLMNPC_AUTHORING_SANDBOX_PREFLIGHT_REJECTED")
		: Error;
	Result.ErrorCode = FName(*Result.ErrorMessage);
	return Result;
}
}

bool FLLMNPCAuthoringSandbox::IsBuildAvailable()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return true;
#endif
}

FString FLLMNPCAuthoringSandbox::BuildCanonicalTargetRef(
	FName TargetSlot
)
{
	return TargetSlot.IsNone()
		? FString()
		: TargetSlot.ToString();
}

FLLMNPCAuthoringSandboxPreflightResult
FLLMNPCAuthoringSandbox::RunFullPreflight(
	const FLLMNPCAuthoringSandboxRequest& Request
)
{
	if (!IsBuildAvailable())
	{
		return Reject(
			ELLMNPCAuthoringSandboxStage::Idle,
			TEXT("LLMNPC_AUTHORING_SANDBOX_SHIPPING_DISABLED")
		);
	}
	if (!Request.SkeletonProfile)
	{
		return Reject(
			ELLMNPCAuthoringSandboxStage::Idle,
			TEXT("LLMNPC_AUTHORING_SANDBOX_PROFILE_MISSING")
		);
	}
	FString Error;
	if (!Request.SkeletonProfile->ValidateProfile(Error))
	{
		return Reject(
			ELLMNPCAuthoringSandboxStage::Idle,
			Error.IsEmpty()
				? TEXT("LLMNPC_AUTHORING_SANDBOX_PROFILE_INVALID")
				: Error
		);
	}

	FLLMNPCSkeletonCapabilitySnapshot Capability;
	const FLLMNPCSkeletonCapabilityBuildResult CapabilityResult =
		FLLMNPCSkeletonCapabilityBuilder::Build(
			*Request.SkeletonProfile,
			Request.ControlManifest,
			Capability
		);
	if (!CapabilityResult.bSucceeded)
	{
		return Reject(
			ELLMNPCAuthoringSandboxStage::Validating,
			CapabilityResult.Errors.IsEmpty()
				? TEXT("LLMNPC_AUTHORING_SANDBOX_CAPABILITY_BUILD_FAILED")
				: CapabilityResult.Errors[0]
		);
	}

	FLLMNPCMotionRecipe Recipe;
	if (!FLLMNPCMotionRecipeParser::Parse(
		Request.RecipeJson,
		Recipe,
		Error
	))
	{
		return Reject(ELLMNPCAuthoringSandboxStage::Parsing, Error);
	}

	FLLMNPCMotionRecipeValidationContext ValidationContext;
	ValidationContext.Mode = ELLMNPCMotionRecipeMode::AuthoringSandbox;
	ValidationContext.ActiveBlockedStates = Request.ActiveBlockedStates;
	ValidationContext.MaxDurationSeconds = FMath::Clamp(
		Request.MaxDurationSeconds,
		0.05f,
		LLMNPCMotionRecipe::DefaultMaxDurationSeconds
	);
	ValidationContext.MaxPrimitiveCount = FMath::Clamp(
		Request.MaxPrimitiveCount,
		1,
		LLMNPCMotionRecipe::DefaultMaxPrimitiveCount
	);
	for (const TPair<FName, FString>& Binding : Request.TargetBindings)
	{
		if (!Binding.Key.IsNone())
		{
			ValidationContext.AllowedTargetSlots.Add(Binding.Key);
		}
	}

	FLLMNPCAuthoringSandboxPreflightResult Result;
	Result.Stage = ELLMNPCAuthoringSandboxStage::Validating;
	if (!FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
		Recipe,
		Capability,
		FLLMNPCMotionPrimitiveRegistry::Get(),
		ValidationContext,
		Result.RecipeValidation
	))
	{
		return Reject(
			ELLMNPCAuthoringSandboxStage::Validating,
			Result.RecipeValidation.ErrorCode
		);
	}
	if (!FLLMNPCMotionRecipeCanonicalizer::BuildCanonicalJson(
		Recipe,
		Result.CanonicalRecipeJson,
		Error
	))
	{
		return Reject(ELLMNPCAuthoringSandboxStage::Validating, Error);
	}

	Result.Stage = ELLMNPCAuthoringSandboxStage::Compiling;
	FLLMNPCMotionRecipeCompileContext CompileContext;
	CompileContext.ValidationContext = ValidationContext;
	CompileContext.TargetBindings = Request.TargetBindings;
	CompileContext.ControlManifest = Request.ControlManifest;
	FLLMMotionPlan CandidatePlan;
	if (!FLLMNPCMotionRecipeCompiler::Compile(
		Recipe,
		Capability,
		FLLMNPCMotionPrimitiveRegistry::Get(),
		CompileContext,
		CandidatePlan,
		Result.CompiledMetadata,
		Error
	))
	{
		return Reject(ELLMNPCAuthoringSandboxStage::Compiling, Error);
	}

	Result.Stage = ELLMNPCAuthoringSandboxStage::Preflight;
	Result.KinematicReport = FLLMNPCKinematicValidator::ValidatePlan(
		CandidatePlan,
		*Request.SkeletonProfile,
		Request.ControlManifest,
		Capability.CapabilityHash
	);
	if (!Result.KinematicReport.bPassed)
	{
		Result.Stage = ELLMNPCAuthoringSandboxStage::Rejected;
		Result.ErrorCode =
			TEXT("LLMNPC_AUTHORING_SANDBOX_KINEMATIC_PREFLIGHT_FAILED");
		const FLLMNPCKinematicValidationIssue* BlockingIssue =
			Result.KinematicReport.Issues.FindByPredicate(
				[](const FLLMNPCKinematicValidationIssue& Issue)
				{
					return Issue.Severity ==
						ELLMNPCKinematicIssueSeverity::Error;
				}
			);
		Result.ErrorMessage = BlockingIssue
			? FString::Printf(
				TEXT("%s:%s:%s:observed=%.6g:limit=%.6g"),
				*Result.ErrorCode.ToString(),
				*BlockingIssue->Code,
				*BlockingIssue->FieldPath,
				BlockingIssue->ObservedValue,
				BlockingIssue->LimitValue
			)
			: Result.ErrorCode.ToString();
		return Result;
	}

	Result.bPassed = true;
	Result.Stage = ELLMNPCAuthoringSandboxStage::Ready;
	Result.TransientPlan = MoveTemp(CandidatePlan);
	return Result;
}
