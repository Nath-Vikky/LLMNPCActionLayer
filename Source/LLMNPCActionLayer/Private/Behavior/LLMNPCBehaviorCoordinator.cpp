#include "Behavior/LLMNPCBehaviorCoordinator.h"

#include "AIController.h"
#include "Behavior/LLMNPCBehaviorPlanValidator.h"
#include "Context/LLMNPCSceneContextComponent.h"
#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCSettings.h"
#include "Navigation/PathFollowingComponent.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

namespace
{
const FName BehaviorDecisionNone(TEXT("none"));

bool IsActiveState(ELLMNPCBehaviorState State)
{
	return
		State == ELLMNPCBehaviorState::Validating ||
		State == ELLMNPCBehaviorState::Moving ||
		State == ELLMNPCBehaviorState::Facing ||
		State == ELLMNPCBehaviorState::Waiting ||
		State == ELLMNPCBehaviorState::ExecutingAction;
}
}

void ULLMNPCBehaviorCoordinator::Initialize(
	ULLMNPCMotionComponent* InMotionComponent,
	ULLMNPCSceneContextComponent* InSceneContext
)
{
	if (MotionComponent != InMotionComponent || SceneContext != InSceneContext)
	{
		CancelBehavior(TEXT("LLMNPC_BEHAVIOR_RUNTIME_CONTEXT_CHANGED"));
	}
	MotionComponent = InMotionComponent;
	SceneContext = InSceneContext;
}

void ULLMNPCBehaviorCoordinator::Shutdown()
{
	CancelBehavior(TEXT("LLMNPC_BEHAVIOR_SHUTDOWN"));
	UnbindMoveDelegate();
	ActiveController = nullptr;
	ActiveTarget = nullptr;
	SceneContext = nullptr;
	MotionComponent = nullptr;
}

FLLMNPCBehaviorExecutionResult ULLMNPCBehaviorCoordinator::ExecuteModelDecision(
	FLLMNPCModelTurnDecision Decision
)
{
	FLLMNPCBehaviorExecutionResult Result;
	if (IsBehaviorActive())
	{
		Result.ErrorCode = TEXT("LLMNPC_BEHAVIOR_ALREADY_ACTIVE");
		Result.ErrorMessage = Result.ErrorCode.ToString();
		Result.State = DebugState.State;
		return Result;
	}

	if (
		Decision.Action.Decision == BehaviorDecisionNone &&
		Decision.Locomotion.Decision == BehaviorDecisionNone
	)
	{
		Result.bAccepted = true;
		return Result;
	}

	if (!MotionComponent || !MotionComponent->GetWorld())
	{
		Result.ErrorCode = TEXT("LLMNPC_BEHAVIOR_MOTION_COMPONENT_MISSING");
		Result.ErrorMessage = Result.ErrorCode.ToString();
		return Result;
	}

	const ULLMNPCSkeletonProfile* Profile = MotionComponent->SkeletonProfile.LoadSynchronous();
	if (!Profile)
	{
		Result.ErrorCode = TEXT("LLMNPC_BEHAVIOR_SKELETON_PROFILE_MISSING");
		Result.ErrorMessage = Result.ErrorCode.ToString();
		return Result;
	}

	UGameInstance* GameInstance = MotionComponent->GetWorld()->GetGameInstance();
	ULLMNPCTemplateLibrarySubsystem* Library = GameInstance
		? GameInstance->GetSubsystem<ULLMNPCTemplateLibrarySubsystem>()
		: nullptr;
	if (!Library)
	{
		Result.ErrorCode = TEXT("LLMNPC_BEHAVIOR_TEMPLATE_LIBRARY_MISSING");
		Result.ErrorMessage = Result.ErrorCode.ToString();
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

	Result.ResolvedTemplateId = MotionTemplate
		? MotionTemplate->Metadata.TemplateId
		: NAME_None;
	FLLMNPCBehaviorPlan Plan;
	ActivePolicy = BuildPolicy();
	if (!FLLMNPCBehaviorPlanValidator::BuildPlan(
		Decision,
		MotionTemplate,
		Modifiers,
		SceneContext,
		MotionComponent->GetOwner(),
		ActivePolicy,
		Plan,
		Result.ErrorMessage
	))
	{
		Result.ErrorCode = FName(*Result.ErrorMessage);
		return Result;
	}

	if (Plan.Steps.IsEmpty())
	{
		Result.bAccepted = true;
		return Result;
	}

	Result.BehaviorPlanId = Plan.PlanId;
	if (!StartPlan(Plan, Result.ErrorMessage))
	{
		Result.ErrorCode = DebugState.LastErrorCode.IsNone()
			? FName(*Result.ErrorMessage)
			: DebugState.LastErrorCode;
		Result.State = DebugState.State;
		return Result;
	}

	Result.bAccepted = true;
	Result.bBehaviorStarted = true;
	Result.bActionExecuted = DebugState.bActionExecuted;
	Result.State = DebugState.State;
	if (DebugState.State == ELLMNPCBehaviorState::Failed)
	{
		Result.bAccepted = false;
		Result.ErrorCode = DebugState.LastErrorCode;
		Result.ErrorMessage = DebugState.LastErrorMessage;
	}
	return Result;
}

void ULLMNPCBehaviorCoordinator::CancelBehavior(FName Reason)
{
	if (!IsBehaviorActive())
	{
		return;
	}

	UnbindMoveDelegate();
	if (ActiveController)
	{
		ActiveController->StopMovement();
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BehaviorTickHandle);
	}
	DebugState.State = ELLMNPCBehaviorState::Cancelled;
	DebugState.LastErrorCode = Reason.IsNone() ? FName(TEXT("LLMNPC_BEHAVIOR_CANCELLED")) : Reason;
	DebugState.LastErrorMessage = DebugState.LastErrorCode.ToString();
	ActiveController = nullptr;
	ActiveTarget = nullptr;
	OnBehaviorFinished.Broadcast(DebugState);
}

bool ULLMNPCBehaviorCoordinator::IsBehaviorActive() const
{
	return IsActiveState(DebugState.State);
}

UWorld* ULLMNPCBehaviorCoordinator::GetWorld() const
{
	return MotionComponent ? MotionComponent->GetWorld() : nullptr;
}

void ULLMNPCBehaviorCoordinator::BeginDestroy()
{
	Shutdown();
	Super::BeginDestroy();
}

FLLMNPCBehaviorPolicy ULLMNPCBehaviorCoordinator::BuildPolicy() const
{
	FLLMNPCBehaviorPolicy Policy;
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (!Settings)
	{
		return Policy;
	}

	Policy.bNavigationEnabled = Settings->bEnableNavigationIntents;
	Policy.bSpawnDefaultAIController = Settings->bSpawnDefaultAIController;
	Policy.MinAcceptanceRadiusCm = FMath::Max(1.0f, Settings->NavigationMinAcceptanceRadiusCm);
	Policy.MaxAcceptanceRadiusCm = FMath::Max(
		Policy.MinAcceptanceRadiusCm,
		Settings->NavigationMaxAcceptanceRadiusCm
	);
	Policy.DefaultAcceptanceRadiusCm = FMath::Clamp(
		Settings->NavigationDefaultAcceptanceRadiusCm,
		Policy.MinAcceptanceRadiusCm,
		Policy.MaxAcceptanceRadiusCm
	);
	Policy.PlanTimeoutSeconds = FMath::Clamp(Settings->BehaviorPlanTimeoutSeconds, 1.0f, 120.0f);
	Policy.MoveTimeoutSeconds = FMath::Clamp(
		Settings->NavigationMoveTimeoutSeconds,
		1.0f,
		Policy.PlanTimeoutSeconds
	);
	Policy.FaceTimeoutSeconds = FMath::Clamp(
		Settings->TargetFacingTimeoutSeconds,
		0.1f,
		Policy.PlanTimeoutSeconds
	);
	Policy.FacingTurnRateDegreesPerSecond = FMath::Clamp(
		Settings->TargetFacingTurnRateDegreesPerSecond,
		30.0f,
		1080.0f
	);
	Policy.FacingToleranceDegrees = FMath::Clamp(
		Settings->TargetFacingToleranceDegrees,
		1.0f,
		45.0f
	);
	Policy.TickIntervalSeconds = FMath::Clamp(
		Settings->BehaviorTickIntervalSeconds,
		0.01f,
		0.25f
	);
	return Policy;
}

bool ULLMNPCBehaviorCoordinator::StartPlan(
	const FLLMNPCBehaviorPlan& Plan,
	FString& OutError
)
{
	OutError.Reset();
	if (IsBehaviorActive())
	{
		OutError = TEXT("LLMNPC_BEHAVIOR_ALREADY_ACTIVE");
		return false;
	}
	if (!FLLMNPCBehaviorPlanValidator::ValidatePlan(Plan, ActivePolicy, OutError))
	{
		FailPlan(FName(*OutError), OutError);
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		OutError = TEXT("LLMNPC_BEHAVIOR_WORLD_MISSING");
		FailPlan(FName(*OutError), OutError);
		return false;
	}

	ActivePlan = Plan;
	DebugState = FLLMNPCBehaviorDebugState();
	DebugState.State = ELLMNPCBehaviorState::Validating;
	DebugState.ActivePlanId = Plan.PlanId;
	DebugState.ActiveStepIndex = 0;
	DebugState.StepCount = Plan.Steps.Num();
	PlanStartedAtSeconds = World->GetTimeSeconds();
	StepStartedAtSeconds = PlanStartedAtSeconds;
	LastTickAtSeconds = PlanStartedAtSeconds;
	bActionExecutedInPlan = false;
	World->GetTimerManager().SetTimer(
		BehaviorTickHandle,
		this,
		&ULLMNPCBehaviorCoordinator::TickBehavior,
		ActivePolicy.TickIntervalSeconds,
		true
	);
	ExecuteCurrentStep();
	return DebugState.State != ELLMNPCBehaviorState::Failed;
}

void ULLMNPCBehaviorCoordinator::ExecuteCurrentStep()
{
	if (!ActivePlan.Steps.IsValidIndex(DebugState.ActiveStepIndex))
	{
		CompletePlan();
		return;
	}

	const FLLMNPCBehaviorStep& Step = ActivePlan.Steps[DebugState.ActiveStepIndex];
	StepStartedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	DebugState.ActiveStepKind = Step.Kind;
	DebugState.ActiveTargetRef = Step.TargetRef;
	DebugState.ActiveTemplateId = Step.TemplateId;

	switch (Step.Kind)
	{
	case ELLMNPCBehaviorStepKind::MoveToTarget:
		BeginMove(Step);
		break;
	case ELLMNPCBehaviorStepKind::FaceTarget:
		BeginFacing(Step);
		break;
	case ELLMNPCBehaviorStepKind::PlayTemplate:
		DebugState.State = ELLMNPCBehaviorState::ExecutingAction;
		if (!MotionComponent || !MotionComponent->SubmitPublishedTemplate(
			Step.TemplateId,
			Step.TemplateModifiers
		))
		{
			const FString MotionError = MotionComponent
				? MotionComponent->LastValidationError
				: FString(TEXT("LLMNPC_BEHAVIOR_MOTION_COMPONENT_MISSING"));
			FailPlan(
				MotionError.IsEmpty()
					? FName(TEXT("LLMNPC_BEHAVIOR_ACTION_REJECTED"))
					: FName(*MotionError),
				MotionError
			);
			return;
		}
		bActionExecutedInPlan = true;
		DebugState.bActionExecuted = true;
		AdvanceStep();
		break;
	case ELLMNPCBehaviorStepKind::Wait:
		DebugState.State = ELLMNPCBehaviorState::Waiting;
		if (Step.DurationSeconds <= 0.0f)
		{
			AdvanceStep();
		}
		break;
	default:
		FailPlan(TEXT("LLMNPC_BEHAVIOR_STEP_KIND_INVALID"));
		break;
	}
}

void ULLMNPCBehaviorCoordinator::BeginMove(const FLLMNPCBehaviorStep& Step)
{
	AActor* OwnerActor = MotionComponent ? MotionComponent->GetOwner() : nullptr;
	APawn* Pawn = Cast<APawn>(OwnerActor);
	if (!Pawn)
	{
		FailPlan(TEXT("LLMNPC_BEHAVIOR_OWNER_NOT_PAWN"));
		return;
	}

	ActiveTarget = ResolveTarget(Step.TargetRef);
	if (!ActiveTarget)
	{
		FailPlan(TEXT("LLMNPC_BEHAVIOR_TARGET_NOT_AVAILABLE"));
		return;
	}

	ActiveController = Cast<AAIController>(Pawn->GetController());
	if (!ActiveController && !Pawn->GetController() && ActivePolicy.bSpawnDefaultAIController)
	{
		Pawn->SpawnDefaultController();
		ActiveController = Cast<AAIController>(Pawn->GetController());
	}
	if (!ActiveController)
	{
		FailPlan(TEXT("LLMNPC_BEHAVIOR_AI_CONTROLLER_MISSING"));
		return;
	}

	UPathFollowingComponent* PathFollowing = ActiveController->GetPathFollowingComponent();
	if (!PathFollowing)
	{
		FailPlan(TEXT("LLMNPC_BEHAVIOR_PATH_FOLLOWING_MISSING"));
		return;
	}

	UnbindMoveDelegate();
	const TWeakObjectPtr<ULLMNPCBehaviorCoordinator> WeakThis(this);
	MoveFinishedHandle = PathFollowing->OnRequestFinished.AddLambda(
		[WeakThis](FAIRequestID RequestId, const FPathFollowingResult& PathResult)
		{
			ULLMNPCBehaviorCoordinator* Self = WeakThis.Get();
			if (
				!Self ||
				Self->DebugState.State != ELLMNPCBehaviorState::Moving ||
				RequestId.GetID() != Self->ActiveMoveRequestId
			)
			{
				return;
			}

			Self->UnbindMoveDelegate();
			Self->ActiveMoveRequestId = 0;
			if (PathResult.IsSuccess())
			{
				Self->AdvanceStep();
			}
			else
			{
				Self->FailPlan(TEXT("LLMNPC_BEHAVIOR_MOVE_FAILED"));
			}
		}
	);

	FAIMoveRequest MoveRequest;
	MoveRequest.SetGoalActor(ActiveTarget);
	MoveRequest.SetAcceptanceRadius(Step.AcceptanceRadiusCm);
	MoveRequest.SetUsePathfinding(true);
	MoveRequest.SetAllowPartialPath(false);
	MoveRequest.SetCanStrafe(false);
	MoveRequest.SetReachTestIncludesAgentRadius(true);
	MoveRequest.SetReachTestIncludesGoalRadius(true);
	const FPathFollowingRequestResult RequestResult = ActiveController->MoveTo(MoveRequest);
	if (RequestResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		UnbindMoveDelegate();
		AdvanceStep();
		return;
	}
	if (RequestResult.Code != EPathFollowingRequestResult::RequestSuccessful)
	{
		UnbindMoveDelegate();
		FailPlan(TEXT("LLMNPC_BEHAVIOR_MOVE_REQUEST_REJECTED"));
		return;
	}

	ActiveMoveRequestId = RequestResult.MoveId.GetID();
	DebugState.State = ELLMNPCBehaviorState::Moving;
}

void ULLMNPCBehaviorCoordinator::BeginFacing(const FLLMNPCBehaviorStep& Step)
{
	ActiveTarget = ResolveTarget(Step.TargetRef);
	if (!ActiveTarget)
	{
		FailPlan(TEXT("LLMNPC_BEHAVIOR_TARGET_NOT_AVAILABLE"));
		return;
	}
	DebugState.State = ELLMNPCBehaviorState::Facing;
}

void ULLMNPCBehaviorCoordinator::TickBehavior()
{
	if (!IsBehaviorActive())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		FailPlan(TEXT("LLMNPC_BEHAVIOR_WORLD_MISSING"));
		return;
	}

	const double NowSeconds = World->GetTimeSeconds();
	const float DeltaSeconds = FMath::Max(0.0f, static_cast<float>(NowSeconds - LastTickAtSeconds));
	LastTickAtSeconds = NowSeconds;
	if (NowSeconds - PlanStartedAtSeconds > ActivePlan.TimeoutSeconds)
	{
		FailPlan(TEXT("LLMNPC_BEHAVIOR_PLAN_TIMED_OUT"));
		return;
	}
	if (!ActivePlan.Steps.IsValidIndex(DebugState.ActiveStepIndex))
	{
		CompletePlan();
		return;
	}

	const FLLMNPCBehaviorStep& Step = ActivePlan.Steps[DebugState.ActiveStepIndex];
	if (NowSeconds - StepStartedAtSeconds > Step.TimeoutSeconds)
	{
		FailPlan(TEXT("LLMNPC_BEHAVIOR_STEP_TIMED_OUT"));
		return;
	}

	if (DebugState.State == ELLMNPCBehaviorState::Facing)
	{
		AActor* OwnerActor = MotionComponent ? MotionComponent->GetOwner() : nullptr;
		ActiveTarget = ResolveTarget(Step.TargetRef);
		if (!OwnerActor || !ActiveTarget)
		{
			FailPlan(TEXT("LLMNPC_BEHAVIOR_TARGET_NOT_AVAILABLE"));
			return;
		}

		const FVector ToTarget = ActiveTarget->GetActorLocation() - OwnerActor->GetActorLocation();
		if (ToTarget.SizeSquared2D() <= KINDA_SMALL_NUMBER)
		{
			AdvanceStep();
			return;
		}

		FRotator DesiredRotation = ToTarget.Rotation();
		DesiredRotation.Pitch = 0.0f;
		DesiredRotation.Roll = 0.0f;
		FRotator CurrentRotation = OwnerActor->GetActorRotation();
		CurrentRotation.Pitch = 0.0f;
		CurrentRotation.Roll = 0.0f;
		const float YawError = FMath::Abs(FMath::FindDeltaAngleDegrees(
			CurrentRotation.Yaw,
			DesiredRotation.Yaw
		));
		if (YawError <= ActivePolicy.FacingToleranceDegrees)
		{
			OwnerActor->SetActorRotation(DesiredRotation);
			AdvanceStep();
			return;
		}

		OwnerActor->SetActorRotation(FMath::RInterpConstantTo(
			CurrentRotation,
			DesiredRotation,
			DeltaSeconds,
			ActivePolicy.FacingTurnRateDegreesPerSecond
		));
	}
	else if (
		DebugState.State == ELLMNPCBehaviorState::Waiting &&
		NowSeconds - StepStartedAtSeconds >= Step.DurationSeconds
	)
	{
		AdvanceStep();
	}
}

void ULLMNPCBehaviorCoordinator::AdvanceStep()
{
	++DebugState.ActiveStepIndex;
	ActiveTarget = nullptr;
	ExecuteCurrentStep();
}

void ULLMNPCBehaviorCoordinator::CompletePlan()
{
	UnbindMoveDelegate();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BehaviorTickHandle);
	}
	DebugState.State = ELLMNPCBehaviorState::Completed;
	DebugState.ActiveStepIndex = ActivePlan.Steps.Num();
	DebugState.ActiveTargetRef.Reset();
	DebugState.ActiveTemplateId = NAME_None;
	DebugState.bActionExecuted = bActionExecutedInPlan;
	DebugState.LastErrorCode = NAME_None;
	DebugState.LastErrorMessage.Reset();
	ActiveController = nullptr;
	ActiveTarget = nullptr;
	OnBehaviorFinished.Broadcast(DebugState);
}

void ULLMNPCBehaviorCoordinator::FailPlan(FName ErrorCode, const FString& ErrorMessage)
{
	UnbindMoveDelegate();
	if (ActiveController)
	{
		ActiveController->StopMovement();
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BehaviorTickHandle);
	}
	DebugState.State = ELLMNPCBehaviorState::Failed;
	DebugState.LastErrorCode = ErrorCode.IsNone()
		? FName(TEXT("LLMNPC_BEHAVIOR_FAILED"))
		: ErrorCode;
	DebugState.LastErrorMessage = ErrorMessage.IsEmpty()
		? DebugState.LastErrorCode.ToString()
		: ErrorMessage;
	DebugState.bActionExecuted = bActionExecutedInPlan;
	ActiveController = nullptr;
	ActiveTarget = nullptr;
	OnBehaviorFinished.Broadcast(DebugState);
}

AActor* ULLMNPCBehaviorCoordinator::ResolveTarget(const FString& TargetRef) const
{
	return SceneContext ? SceneContext->ResolveSceneTarget(TargetRef) : nullptr;
}

void ULLMNPCBehaviorCoordinator::UnbindMoveDelegate()
{
	if (MoveFinishedHandle.IsValid() && ActiveController)
	{
		if (UPathFollowingComponent* PathFollowing = ActiveController->GetPathFollowingComponent())
		{
			PathFollowing->OnRequestFinished.Remove(MoveFinishedHandle);
		}
	}
	MoveFinishedHandle.Reset();
}
