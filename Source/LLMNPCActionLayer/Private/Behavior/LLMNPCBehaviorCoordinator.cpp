#include "Behavior/LLMNPCBehaviorCoordinator.h"

#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Engine/GameInstance.h"
#include "LLMNPCMotionComponent.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

void ULLMNPCBehaviorCoordinator::Initialize(ULLMNPCMotionComponent* InMotionComponent)
{
	MotionComponent = InMotionComponent;
}

FLLMNPCBehaviorExecutionResult ULLMNPCBehaviorCoordinator::ExecuteModelDecision(
	FLLMNPCModelTurnDecision Decision
)
{
	static const FName DecisionNone(TEXT("none"));
	FLLMNPCBehaviorExecutionResult Result;
	if (Decision.Action.Decision == DecisionNone && Decision.Locomotion.Decision == DecisionNone)
	{
		Result.bAccepted = true;
		return Result;
	}

	if (!MotionComponent || !MotionComponent->GetWorld())
	{
		Result.ErrorCode = TEXT("LLMNPC_BEHAVIOR_MOTION_COMPONENT_MISSING");
		return Result;
	}

	const ULLMNPCSkeletonProfile* Profile = MotionComponent->SkeletonProfile.LoadSynchronous();
	if (!Profile)
	{
		Result.ErrorCode = TEXT("LLMNPC_BEHAVIOR_SKELETON_PROFILE_MISSING");
		return Result;
	}

	UGameInstance* GameInstance = MotionComponent->GetWorld()->GetGameInstance();
	ULLMNPCTemplateLibrarySubsystem* Library = GameInstance
		? GameInstance->GetSubsystem<ULLMNPCTemplateLibrarySubsystem>()
		: nullptr;
	if (!Library)
	{
		Result.ErrorCode = TEXT("LLMNPC_BEHAVIOR_TEMPLATE_LIBRARY_MISSING");
		return Result;
	}

	const ULLMNPCMotionTemplate* MotionTemplate = nullptr;
	FLLMNPCTemplateModifiers Modifiers;
	if (!FLLMNPCModelTurnValidator::ValidateAndResolve(
		Decision,
		*Library,
		Profile->ProfileId,
		MotionTemplate,
		Modifiers,
		Result.ErrorMessage
	))
	{
		Result.ErrorCode = FName(*Result.ErrorMessage);
		return Result;
	}

	Result.bAccepted = true;
	if (!MotionTemplate)
	{
		return Result;
	}

	Result.ResolvedTemplateId = MotionTemplate->Metadata.TemplateId;
	Result.bActionExecuted = MotionComponent->SubmitPublishedTemplate(
		MotionTemplate->Metadata.TemplateId,
		Modifiers
	);
	if (!Result.bActionExecuted)
	{
		Result.bAccepted = false;
		Result.ErrorMessage = MotionComponent->LastValidationError;
		Result.ErrorCode = Result.ErrorMessage.IsEmpty()
			? FName(TEXT("LLMNPC_BEHAVIOR_ACTION_REJECTED"))
			: FName(*Result.ErrorMessage);
	}
	return Result;
}
