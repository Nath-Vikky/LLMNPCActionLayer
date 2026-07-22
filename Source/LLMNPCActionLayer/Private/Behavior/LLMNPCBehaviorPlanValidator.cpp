#include "Behavior/LLMNPCBehaviorPlanValidator.h"

#include "Context/LLMNPCSceneContextComponent.h"
#include "GameFramework/Actor.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
const FName BehaviorPlanDecisionNone(TEXT("none"));
const FName BehaviorPlanDecisionMoveTo(TEXT("move_to"));

bool IsPositiveFinite(float Value)
{
	return FMath::IsFinite(Value) && Value > 0.0f;
}
}

bool FLLMNPCBehaviorPlanValidator::BuildPlan(
	const FLLMNPCModelTurnDecision& Decision,
	const ULLMNPCMotionTemplate* MotionTemplate,
	const FLLMNPCTemplateModifiers& Modifiers,
	const ULLMNPCSceneContextComponent* SceneContext,
	const AActor* OwnerActor,
	const FLLMNPCBehaviorPolicy& Policy,
	FLLMNPCBehaviorPlan& OutPlan,
	FString& OutError
)
{
	OutPlan = FLLMNPCBehaviorPlan();
	OutPlan.PlanId = FGuid::NewGuid();
	OutPlan.TimeoutSeconds = Policy.PlanTimeoutSeconds;
	OutError.Reset();

	if (Decision.Locomotion.Decision != BehaviorPlanDecisionNone)
	{
		if (!Policy.bNavigationEnabled)
		{
			OutError = TEXT("LLMNPC_BEHAVIOR_NAVIGATION_DISABLED");
			return false;
		}
		if (Decision.Locomotion.Decision != BehaviorPlanDecisionMoveTo)
		{
			OutError = TEXT("LLMNPC_BEHAVIOR_LOCOMOTION_UNSUPPORTED");
			return false;
		}

		const FString TargetRef = Decision.Locomotion.TargetRef.TrimStartAndEnd();
		AActor* TargetActor = SceneContext ? SceneContext->ResolveSceneTarget(TargetRef) : nullptr;
		if (TargetRef.IsEmpty() || !IsValid(TargetActor))
		{
			OutError = TEXT("LLMNPC_BEHAVIOR_TARGET_NOT_AVAILABLE");
			return false;
		}
		if (TargetActor == OwnerActor)
		{
			OutError = TEXT("LLMNPC_BEHAVIOR_TARGET_IS_SELF");
			return false;
		}

		FLLMNPCBehaviorStep& MoveStep = OutPlan.Steps.AddDefaulted_GetRef();
		MoveStep.Kind = ELLMNPCBehaviorStepKind::MoveToTarget;
		MoveStep.TargetRef = TargetRef;
		MoveStep.AcceptanceRadiusCm = Decision.Locomotion.AcceptanceRadiusCm <= 0.0f
			? Policy.DefaultAcceptanceRadiusCm
			: FMath::Clamp(
				Decision.Locomotion.AcceptanceRadiusCm,
				Policy.MinAcceptanceRadiusCm,
				Policy.MaxAcceptanceRadiusCm
			);
		MoveStep.TimeoutSeconds = Policy.MoveTimeoutSeconds;

		FLLMNPCBehaviorStep& FaceStep = OutPlan.Steps.AddDefaulted_GetRef();
		FaceStep.Kind = ELLMNPCBehaviorStepKind::FaceTarget;
		FaceStep.TargetRef = TargetRef;
		FaceStep.TimeoutSeconds = Policy.FaceTimeoutSeconds;
	}

	if (MotionTemplate)
	{
		FLLMNPCBehaviorStep& ActionStep = OutPlan.Steps.AddDefaulted_GetRef();
		ActionStep.Kind = ELLMNPCBehaviorStepKind::PlayTemplate;
		ActionStep.TemplateId = MotionTemplate->Metadata.TemplateId;
		ActionStep.TemplateModifiers = Modifiers;
		ActionStep.TimeoutSeconds = FMath::Min(Policy.PlanTimeoutSeconds, 5.0f);
	}

	return ValidatePlan(OutPlan, Policy, OutError);
}

bool FLLMNPCBehaviorPlanValidator::ValidatePlan(
	const FLLMNPCBehaviorPlan& Plan,
	const FLLMNPCBehaviorPolicy& Policy,
	FString& OutError
)
{
	OutError.Reset();
	if (!Plan.PlanId.IsValid())
	{
		OutError = TEXT("LLMNPC_BEHAVIOR_PLAN_ID_INVALID");
		return false;
	}
	if (!IsPositiveFinite(Plan.TimeoutSeconds) || Plan.TimeoutSeconds > 120.0f)
	{
		OutError = TEXT("LLMNPC_BEHAVIOR_PLAN_TIMEOUT_INVALID");
		return false;
	}
	if (Plan.Steps.Num() > 8)
	{
		OutError = TEXT("LLMNPC_BEHAVIOR_PLAN_TOO_MANY_STEPS");
		return false;
	}

	int32 MoveCount = 0;
	int32 FaceCount = 0;
	int32 PlayCount = 0;
	int32 MoveIndex = INDEX_NONE;
	int32 FaceIndex = INDEX_NONE;
	int32 PlayIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Plan.Steps.Num(); ++Index)
	{
		const FLLMNPCBehaviorStep& Step = Plan.Steps[Index];
		if (!IsPositiveFinite(Step.TimeoutSeconds) || Step.TimeoutSeconds > Plan.TimeoutSeconds)
		{
			OutError = TEXT("LLMNPC_BEHAVIOR_STEP_TIMEOUT_INVALID");
			return false;
		}

		switch (Step.Kind)
		{
		case ELLMNPCBehaviorStepKind::MoveToTarget:
			++MoveCount;
			MoveIndex = Index;
			if (
				Step.TargetRef.TrimStartAndEnd().IsEmpty() ||
				!FMath::IsFinite(Step.AcceptanceRadiusCm) ||
				Step.AcceptanceRadiusCm < Policy.MinAcceptanceRadiusCm ||
				Step.AcceptanceRadiusCm > Policy.MaxAcceptanceRadiusCm
			)
			{
				OutError = TEXT("LLMNPC_BEHAVIOR_MOVE_POLICY_INVALID");
				return false;
			}
			break;
		case ELLMNPCBehaviorStepKind::FaceTarget:
			++FaceCount;
			FaceIndex = Index;
			if (Step.TargetRef.TrimStartAndEnd().IsEmpty())
			{
				OutError = TEXT("LLMNPC_BEHAVIOR_FACE_TARGET_MISSING");
				return false;
			}
			break;
		case ELLMNPCBehaviorStepKind::PlayTemplate:
			++PlayCount;
			PlayIndex = Index;
			if (Step.TemplateId.IsNone())
			{
				OutError = TEXT("LLMNPC_BEHAVIOR_TEMPLATE_MISSING");
				return false;
			}
			break;
		case ELLMNPCBehaviorStepKind::Wait:
			if (!FMath::IsFinite(Step.DurationSeconds) || Step.DurationSeconds < 0.0f)
			{
				OutError = TEXT("LLMNPC_BEHAVIOR_WAIT_DURATION_INVALID");
				return false;
			}
			break;
		default:
			OutError = TEXT("LLMNPC_BEHAVIOR_STEP_KIND_INVALID");
			return false;
		}
	}

	if (MoveCount > 1 || FaceCount > 1 || PlayCount > 1)
	{
		OutError = TEXT("LLMNPC_BEHAVIOR_DUPLICATE_CORE_STEP");
		return false;
	}
	if (FaceIndex != INDEX_NONE && (MoveIndex == INDEX_NONE || FaceIndex < MoveIndex))
	{
		OutError = TEXT("LLMNPC_BEHAVIOR_FACE_ORDER_INVALID");
		return false;
	}
	if (
		PlayIndex != INDEX_NONE &&
		((MoveIndex != INDEX_NONE && PlayIndex < MoveIndex) || (FaceIndex != INDEX_NONE && PlayIndex < FaceIndex))
	)
	{
		OutError = TEXT("LLMNPC_BEHAVIOR_ACTION_ORDER_INVALID");
		return false;
	}
	return true;
}
