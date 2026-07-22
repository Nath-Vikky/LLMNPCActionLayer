#include "Dialogue/LLMNPCDialogueComponent.h"

#include "Async/Async.h"
#include "Behavior/LLMNPCBehaviorCoordinator.h"
#include "Context/LLMNPCEmotionComponent.h"
#include "Context/LLMNPCPersonalityProfile.h"
#include "Context/LLMNPCRelationshipComponent.h"
#include "Context/LLMNPCSceneContextComponent.h"
#include "Dialogue/LLMNPCConversationSession.h"
#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCSettings.h"
#include "Providers/LLMNPCBackendProxyProvider.h"
#include "Providers/LLMNPCDeepSeekProvider.h"
#include "Providers/LLMNPCMockProvider.h"
#include "Selection/LLMNPCCandidateRetriever.h"
#include "Selection/LLMNPCSelectionAnalyticsSubsystem.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Style/LLMNPCStyleResolver.h"
#include "Templates/LLMNPCTemplateCandidate.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"
#include "UI/LLMNPCChatWidget.h"

ULLMNPCDialogueComponent::ULLMNPCDialogueComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void ULLMNPCDialogueComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureRuntimeObjects();
	if (bAutoCreateChatWidget && GetWorld())
	{
		CreateChatWidget(GetWorld()->GetFirstPlayerController());
	}
}

void ULLMNPCDialogueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelActiveRequest();
	if (BehaviorCoordinator)
	{
		BehaviorCoordinator->Shutdown();
	}
	ModelProvider.Reset();
	ChatWidget = nullptr;
	Super::EndPlay(EndPlayReason);
}

bool ULLMNPCDialogueComponent::SendPlayerMessage(const FString& Message)
{
	const FString CleanMessage = Message.TrimStartAndEnd();
	if (CleanMessage.IsEmpty())
	{
		return false;
	}
	if (bRequestInFlight)
	{
		LastTurnResult.ErrorCode = TEXT("LLMNPC_DIALOGUE_REQUEST_IN_FLIGHT");
		LastTurnResult.ErrorMessage = TEXT("A dialogue request is already in flight.");
		return false;
	}

	EnsureRuntimeObjects();
	if (!ConversationSession || !ModelProvider)
	{
		CompleteFailure(
			TEXT("LLMNPC_DIALOGUE_PROVIDER_UNAVAILABLE"),
			TEXT("No model provider is available."),
			true
		);
		return false;
	}

	const FLLMNPCConversationMessage PlayerMessage = ConversationSession->AddMessage(
		ELLMNPCDialogueRole::Player,
		CleanMessage
	);
	OnMessageAdded.Broadcast(PlayerMessage);

	TArray<FLLMNPCTemplateCandidate> SourceCandidates;
	if (GetWorld() && GetWorld()->GetGameInstance())
	{
		if (ULLMNPCTemplateLibrarySubsystem* Library =
			GetWorld()->GetGameInstance()->GetSubsystem<ULLMNPCTemplateLibrarySubsystem>())
		{
			Library->QueryRuntimeCandidates(ResolveSkeletonProfileId(), SourceCandidates);
		}
	}

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	FLLMNPCCandidateRetrievalRequest RetrievalRequest;
	RetrievalRequest.UserMessage = CleanMessage;
	RetrievalRequest.SourceCandidates = MoveTemp(SourceCandidates);
	RetrievalRequest.Context = GetSelectionContextSnapshot();
	if (MotionComponent)
	{
		MotionComponent->SetAmbientStyle(ULLMNPCStyleResolver::ResolveRecommendedStyle(
			RetrievalRequest.Context,
			{ TEXT("neutral"), TEXT("friendly"), TEXT("subtle"), TEXT("excited") }
		));
	}
	RetrievalRequest.ActionHistory = ConversationSession->GetActionHistory();
	RetrievalRequest.NowSeconds = FPlatformTime::Seconds();
	RetrievalRequest.MaxCandidates = Settings ? Settings->MaxContextCandidates : 8;
	RetrievalRequest.RepeatSuppressionSeconds = Settings ? Settings->RepeatSuppressionSeconds : 2.0f;
	FLLMNPCCandidateRetrievalResult Retrieval = ULLMNPCCandidateRetriever::Retrieve(RetrievalRequest);
	ActiveOfferedCandidates = MoveTemp(Retrieval.Candidates);
	ActiveCandidateExclusions = MoveTemp(Retrieval.Exclusions);
	LastOfferedCandidates = ActiveOfferedCandidates;

	ActiveRequest = FLLMNPCModelTurnRequest();
	ActiveRequest.RequestId = FGuid::NewGuid();
	ActiveRequest.SessionId = ConversationSession->GetSessionId();
	ActiveRequest.NPCId = NPCId;
	ActiveRequest.UserMessage = CleanMessage;
	const FString PromptVersion = Settings
		? Settings->SelectionPromptVersion
		: FString(TEXT("llmnpc.selection_prompt.v1"));
	ActiveRequest.ContextJson = ConversationSession->BuildContextualRequestJson(
		ActiveRequest.RequestId,
		ActiveOfferedCandidates,
		RetrievalRequest.Context,
		PromptVersion
	);
	if (GetWorld() && GetWorld()->GetGameInstance())
	{
		SelectionAnalytics = GetWorld()->GetGameInstance()->GetSubsystem<ULLMNPCSelectionAnalyticsSubsystem>();
	}
	if (SelectionAnalytics)
	{
		TArray<FName> OfferedIds;
		for (const FLLMNPCTemplateCandidate& Candidate : ActiveOfferedCandidates)
		{
			OfferedIds.Add(Candidate.SelectionId);
		}
		SelectionAnalytics->BeginSelection(
			ActiveRequest.RequestId,
			NPCId,
			LastProviderId,
			PromptVersion,
			OfferedIds,
			ActiveCandidateExclusions.Num(),
			ActiveRequest.ContextJson
		);
	}
	bRequestInFlight = true;
	LastTurnResult = FLLMNPCDialogueTurnResult();
	LastTurnResult.RequestId = ActiveRequest.RequestId;
	SetState(ELLMNPCDialogueState::Sending);

	const TWeakObjectPtr<ULLMNPCDialogueComponent> WeakThis(this);
	ModelProvider->SendTurn(
		ActiveRequest,
		[WeakThis](const FLLMNPCModelTurnResult& Result)
		{
			if (IsInGameThread())
			{
				if (ULLMNPCDialogueComponent* Self = WeakThis.Get())
				{
					Self->HandleProviderResult(Result, false);
				}
			}
			else
			{
				AsyncTask(
					ENamedThreads::GameThread,
					[WeakThis, Result]()
					{
						if (ULLMNPCDialogueComponent* Self = WeakThis.Get())
						{
							Self->HandleProviderResult(Result, false);
						}
					}
				);
			}
		}
	);
	return true;
}

void ULLMNPCDialogueComponent::CancelActiveRequest()
{
	if (!bRequestInFlight)
	{
		return;
	}

	const FGuid CancelledRequestId = ActiveRequest.RequestId;
	bRequestInFlight = false;
	if (ModelProvider)
	{
		ModelProvider->CancelRequest(CancelledRequestId);
	}
	LastTurnResult = FLLMNPCDialogueTurnResult();
	LastTurnResult.RequestId = CancelledRequestId;
	LastTurnResult.ErrorCode = TEXT("LLMNPC_PROVIDER_CANCELLED");
	CompleteAnalytics(TEXT("cancelled"), LastTurnResult.ErrorCode, false);
	ActiveRequest = FLLMNPCModelTurnRequest();
	ResetActiveSelection();
	SetState(ELLMNPCDialogueState::Cancelled);
	OnTurnCompleted.Broadcast(LastTurnResult);
}

void ULLMNPCDialogueComponent::CancelActiveBehavior()
{
	if (BehaviorCoordinator)
	{
		BehaviorCoordinator->CancelBehavior();
	}
}

void ULLMNPCDialogueComponent::ResetConversation()
{
	CancelActiveRequest();
	CancelActiveBehavior();
	EnsureRuntimeObjects();
	if (ConversationSession)
	{
		ConversationSession->ResetSession();
	}
	LastTurnResult = FLLMNPCDialogueTurnResult();
	SetState(ELLMNPCDialogueState::Idle);
}

void ULLMNPCDialogueComponent::SetProviderKind(ELLMNPCModelProviderKind NewProviderKind)
{
	if (ProviderKind == NewProviderKind)
	{
		return;
	}
	CancelActiveRequest();
	ProviderKind = NewProviderKind;
	RecreateProvider();
}

void ULLMNPCDialogueComponent::SetMotionComponent(ULLMNPCMotionComponent* InMotionComponent)
{
	MotionComponent = InMotionComponent;
	EnsureRuntimeObjects();
	if (BehaviorCoordinator)
	{
		BehaviorCoordinator->Initialize(MotionComponent, SceneContextComponent);
	}
}

void ULLMNPCDialogueComponent::RegisterTarget(const FString& TargetRef, AActor* TargetActor)
{
	EnsureRuntimeObjects();
	if (MotionComponent)
	{
		MotionComponent->RegisterTarget(TargetRef, TargetActor);
	}
	if (SceneContextComponent)
	{
		SceneContextComponent->RegisterSceneTarget(
			TargetRef,
			TargetActor,
			TEXT("generic"),
			TArray<FName>(),
			0.5f
		);
	}
}

void ULLMNPCDialogueComponent::RegisterSceneTarget(
	const FString& TargetRef,
	AActor* TargetActor,
	FName Category,
	const TArray<FName>& SemanticTags,
	float Salience
)
{
	EnsureRuntimeObjects();
	if (MotionComponent)
	{
		MotionComponent->RegisterTarget(TargetRef, TargetActor);
	}
	if (SceneContextComponent)
	{
		SceneContextComponent->RegisterSceneTarget(
			TargetRef,
			TargetActor,
			Category,
			SemanticTags,
			Salience
		);
	}
}

void ULLMNPCDialogueComponent::SetSceneStateActive(FName StateName, bool bActive)
{
	EnsureRuntimeObjects();
	if (SceneContextComponent)
	{
		SceneContextComponent->SetStateActive(StateName, bActive);
	}
}

ULLMNPCChatWidget* ULLMNPCDialogueComponent::CreateChatWidget(
	APlayerController* PlayerController,
	int32 ZOrder
)
{
	if (!PlayerController)
	{
		return nullptr;
	}

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	UClass* WidgetClass = Settings ? Settings->DefaultChatWidgetClass.LoadSynchronous() : nullptr;
	if (!WidgetClass || !WidgetClass->IsChildOf(ULLMNPCChatWidget::StaticClass()))
	{
		WidgetClass = ULLMNPCChatWidget::StaticClass();
	}

	ChatWidget = CreateWidget<ULLMNPCChatWidget>(PlayerController, WidgetClass);
	if (ChatWidget)
	{
		ChatWidget->BindDialogueComponent(this);
		ChatWidget->AddToViewport(ZOrder);
		ChatWidget->SetDesiredSizeInViewport(FVector2D(520.0f, 560.0f));
		ChatWidget->SetPositionInViewport(FVector2D(32.0f, 80.0f), false);
	}
	return ChatWidget;
}

FLLMNPCDialogueDebugState ULLMNPCDialogueComponent::GetDebugState() const
{
	FLLMNPCDialogueDebugState Debug;
	Debug.State = State;
	Debug.ActiveRequestId = bRequestInFlight ? ActiveRequest.RequestId : FGuid();
	Debug.ProviderId = LastProviderId;
	Debug.LastSelectedActionId = LastTurnResult.SelectedActionId;
	Debug.LastResolvedTemplateId = LastTurnResult.ResolvedTemplateId;
	Debug.LastErrorCode = LastTurnResult.ErrorCode;
	Debug.LastErrorMessage = LastTurnResult.ErrorMessage;
	Debug.MessageCount = ConversationSession ? ConversationSession->GetMessages().Num() : 0;
	return Debug;
}

FLLMNPCBehaviorDebugState ULLMNPCDialogueComponent::GetBehaviorDebugState() const
{
	return BehaviorCoordinator
		? BehaviorCoordinator->GetDebugState()
		: FLLMNPCBehaviorDebugState();
}

FLLMNPCSelectionContextSnapshot ULLMNPCDialogueComponent::GetSelectionContextSnapshot() const
{
	FLLMNPCSelectionContextSnapshot Snapshot;
	if (EmotionComponent)
	{
		Snapshot.Emotion = EmotionComponent->GetEmotionSnapshot();
	}
	if (PersonalityProfile)
	{
		Snapshot.Personality = PersonalityProfile->GetPersonalitySnapshot();
	}
	if (RelationshipComponent)
	{
		Snapshot.Relationship = RelationshipComponent->GetRelationshipSnapshot();
	}
	if (SceneContextComponent)
	{
		Snapshot = SceneContextComponent->AppendToSnapshot(Snapshot);
	}
	return Snapshot;
}

void ULLMNPCDialogueComponent::EnsureRuntimeObjects()
{
	if (!ConversationSession)
	{
		ConversationSession = NewObject<ULLMNPCConversationSession>(this);
		ConversationSession->InitializeSession(NPCId, MaxHistoryMessages);
	}

	if (!MotionComponent && bAutoFindMotionComponent && GetOwner())
	{
		MotionComponent = GetOwner()->FindComponentByClass<ULLMNPCMotionComponent>();
	}

	if (bAutoFindContextComponents && GetOwner())
	{
		if (!EmotionComponent)
		{
			EmotionComponent = GetOwner()->FindComponentByClass<ULLMNPCEmotionComponent>();
		}
		if (!RelationshipComponent)
		{
			RelationshipComponent = GetOwner()->FindComponentByClass<ULLMNPCRelationshipComponent>();
		}
		if (!SceneContextComponent)
		{
			SceneContextComponent = GetOwner()->FindComponentByClass<ULLMNPCSceneContextComponent>();
		}
	}
	if (!SceneContextComponent)
	{
		SceneContextComponent = NewObject<ULLMNPCSceneContextComponent>(this);
	}

	if (!BehaviorCoordinator)
	{
		BehaviorCoordinator = NewObject<ULLMNPCBehaviorCoordinator>(this);
	}
	BehaviorCoordinator->Initialize(MotionComponent, SceneContextComponent);

	if (!ModelProvider)
	{
		RecreateProvider();
	}
}

void ULLMNPCDialogueComponent::RecreateProvider()
{
	ModelProvider.Reset();
	switch (ResolveProviderKind())
	{
	case ELLMNPCModelProviderKind::BackendProxy:
		ModelProvider = MakeShared<FLLMNPCBackendProxyProvider>();
		break;
	case ELLMNPCModelProviderKind::DeepSeekDirectEditorOnly:
		ModelProvider = MakeShared<FLLMNPCDeepSeekProvider>();
		break;
	case ELLMNPCModelProviderKind::Mock:
	case ELLMNPCModelProviderKind::UseProjectSettings:
	default:
		ModelProvider = MakeShared<FLLMNPCMockProvider>();
		break;
	}
	LastProviderId = ModelProvider ? ModelProvider->GetProviderId() : NAME_None;
}

ELLMNPCModelProviderKind ULLMNPCDialogueComponent::ResolveProviderKind() const
{
	if (ProviderKind != ELLMNPCModelProviderKind::UseProjectSettings)
	{
		return ProviderKind;
	}

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	return Settings ? Settings->DefaultModelProvider : ELLMNPCModelProviderKind::Mock;
}

void ULLMNPCDialogueComponent::SetState(ELLMNPCDialogueState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	OnStateChanged.Broadcast(State);
}

void ULLMNPCDialogueComponent::HandleProviderResult(
	const FLLMNPCModelTurnResult& ProviderResult,
	bool bUsedFallback,
	FName OriginalProviderError
)
{
	if (!bRequestInFlight || ProviderResult.RequestId != ActiveRequest.RequestId)
	{
		return;
	}

	LastProviderId = ProviderResult.ProviderId;
	SetState(ELLMNPCDialogueState::Receiving);
	if (!ProviderResult.bSuccess)
	{
		HandleProviderFailure(ProviderResult);
		return;
	}

	SetState(ELLMNPCDialogueState::Parsing);
	FLLMNPCModelTurnDecision Decision;
	FString ParseError;
	const bool bParsed = FLLMNPCModelTurnParser::Parse(
		ProviderResult.ResponseJson,
		Decision,
		ParseError
	);
	CompleteFromDecision(
		MoveTemp(Decision),
		bParsed,
		ParseError,
		bUsedFallback,
		OriginalProviderError
	);
}

void ULLMNPCDialogueComponent::HandleProviderFailure(
	const FLLMNPCModelTurnResult& ProviderResult
)
{
	if (bEnableLocalCommandFallback && ResolveProviderKind() != ELLMNPCModelProviderKind::Mock)
	{
		FLLMNPCModelTurnRequest FallbackRequest = ActiveRequest;
		FallbackRequest.bFallbackRequest = true;
		const TWeakObjectPtr<ULLMNPCDialogueComponent> WeakThis(this);
		FLLMNPCMockProvider FallbackProvider;
		FallbackProvider.SendTurn(
			FallbackRequest,
			[WeakThis, ProviderError = ProviderResult.ErrorCode](const FLLMNPCModelTurnResult& Result)
			{
				if (ULLMNPCDialogueComponent* Self = WeakThis.Get())
				{
					Self->HandleProviderResult(Result, true, ProviderError);
				}
			}
		);
		return;
	}

	CompleteFailure(
		ProviderResult.ErrorCode.IsNone() ? FName(TEXT("LLMNPC_PROVIDER_FAILED")) : ProviderResult.ErrorCode,
		ProviderResult.ErrorMessage,
		true
	);
}

void ULLMNPCDialogueComponent::CompleteFromDecision(
	FLLMNPCModelTurnDecision Decision,
	bool bParseSucceeded,
	const FString& ParseError,
	bool bUsedFallback,
	FName OriginalProviderError
)
{
	LastTurnResult = FLLMNPCDialogueTurnResult();
	LastTurnResult.RequestId = ActiveRequest.RequestId;
	LastTurnResult.bUsedLocalFallback = bUsedFallback;
	if (!Decision.AssistantText.IsEmpty())
	{
		const FLLMNPCConversationMessage AssistantMessage = ConversationSession->AddMessage(
			ELLMNPCDialogueRole::Assistant,
			Decision.AssistantText
		);
		OnMessageAdded.Broadcast(AssistantMessage);
		LastTurnResult.bTextResponseReceived = true;
		LastTurnResult.AssistantText = Decision.AssistantText;
	}

	if (!bParseSucceeded)
	{
		LastTurnResult.ErrorCode = FName(*ParseError);
		LastTurnResult.ErrorMessage = ParseError;
		CompleteAnalytics(TEXT("parse_rejected"), LastTurnResult.ErrorCode, bUsedFallback);
		bRequestInFlight = false;
		ActiveRequest = FLLMNPCModelTurnRequest();
		ResetActiveSelection();
		SetState(ELLMNPCDialogueState::Failed);
		OnTurnCompleted.Broadcast(LastTurnResult);
		return;
	}

	LastTurnResult.SelectedActionId = Decision.Action.TemplateId;
	Decision.Action.RandomSeed = ULLMNPCStyleResolver::BuildDeterministicSeed(
		ConversationSession->GetSessionId(),
		ActiveRequest.RequestId,
		NPCId,
		Decision.Action.TemplateId
	);
	FString SelectionError;
	if (!ULLMNPCCandidateRetriever::ApplySelectionPolicy(
		Decision,
		ActiveOfferedCandidates,
		SelectionError
	))
	{
		LastTurnResult.ErrorCode = FName(*SelectionError);
		LastTurnResult.ErrorMessage = SelectionError;
		CompleteAnalytics(TEXT("selection_rejected"), LastTurnResult.ErrorCode, bUsedFallback);
		bRequestInFlight = false;
		ActiveRequest = FLLMNPCModelTurnRequest();
		ResetActiveSelection();
		SetState(ELLMNPCDialogueState::Failed);
		OnTurnCompleted.Broadcast(LastTurnResult);
		return;
	}
	SetState(ELLMNPCDialogueState::Validating);
	SetState(ELLMNPCDialogueState::Executing);
	const FLLMNPCBehaviorExecutionResult BehaviorResult = BehaviorCoordinator
		? BehaviorCoordinator->ExecuteModelDecision(Decision)
		: FLLMNPCBehaviorExecutionResult();
	LastTurnResult.bActionExecuted = BehaviorResult.bActionExecuted;
	LastTurnResult.bBehaviorStarted = BehaviorResult.bBehaviorStarted;
	LastTurnResult.BehaviorPlanId = BehaviorResult.BehaviorPlanId;
	LastTurnResult.ResolvedTemplateId = BehaviorResult.ResolvedTemplateId;
	if (
		(BehaviorResult.bActionExecuted || BehaviorResult.bBehaviorStarted) &&
		!Decision.Action.TemplateId.IsNone() &&
		ConversationSession
	)
	{
		ConversationSession->AddActionHistory(
			Decision.Action.TemplateId,
			BehaviorResult.ResolvedTemplateId,
			Decision.Action.TargetRef,
			Decision.Action.ReasonTag
		);
	}

	if (!BehaviorResult.bAccepted)
	{
		LastTurnResult.ErrorCode = BehaviorResult.ErrorCode;
		LastTurnResult.ErrorMessage = BehaviorResult.ErrorMessage;
	}
	else if (!OriginalProviderError.IsNone())
	{
		LastTurnResult.ErrorCode = OriginalProviderError;
		LastTurnResult.ErrorMessage = TEXT("The remote provider failed; a local command fallback was used.");
	}

	CompleteAnalytics(
		(BehaviorResult.bActionExecuted || BehaviorResult.bBehaviorStarted)
			? FName(TEXT("executed"))
			: (BehaviorResult.bAccepted ? FName(TEXT("no_action")) : FName(TEXT("execution_rejected"))),
		LastTurnResult.ErrorCode,
		bUsedFallback
	);
	bRequestInFlight = false;
	ActiveRequest = FLLMNPCModelTurnRequest();
	ResetActiveSelection();
	SetState(BehaviorResult.bAccepted ? ELLMNPCDialogueState::Idle : ELLMNPCDialogueState::Failed);
	OnTurnCompleted.Broadcast(LastTurnResult);
}

void ULLMNPCDialogueComponent::CompleteFailure(
	FName ErrorCode,
	const FString& ErrorMessage,
	bool bAddAssistantMessage
)
{
	LastTurnResult = FLLMNPCDialogueTurnResult();
	LastTurnResult.RequestId = ActiveRequest.RequestId;
	LastTurnResult.ErrorCode = ErrorCode;
	LastTurnResult.ErrorMessage = ErrorMessage;
	CompleteAnalytics(TEXT("provider_failed"), ErrorCode, false);
	if (bAddAssistantMessage && ConversationSession)
	{
		const FString FallbackText = TEXT("I cannot respond right now. Please try again.");
		const FLLMNPCConversationMessage AssistantMessage = ConversationSession->AddMessage(
			ELLMNPCDialogueRole::Assistant,
			FallbackText
		);
		OnMessageAdded.Broadcast(AssistantMessage);
		LastTurnResult.bTextResponseReceived = true;
		LastTurnResult.AssistantText = FallbackText;
	}
	bRequestInFlight = false;
	ActiveRequest = FLLMNPCModelTurnRequest();
	ResetActiveSelection();
	SetState(ELLMNPCDialogueState::Failed);
	OnTurnCompleted.Broadcast(LastTurnResult);
}

void ULLMNPCDialogueComponent::CompleteAnalytics(FName Outcome, FName ErrorCode, bool bUsedFallback)
{
	if (SelectionAnalytics && ActiveRequest.RequestId.IsValid())
	{
		SelectionAnalytics->CompleteSelection(
			ActiveRequest.RequestId,
			LastTurnResult.SelectedActionId,
			LastTurnResult.ResolvedTemplateId,
			Outcome,
			ErrorCode,
			bUsedFallback
		);
	}
}

void ULLMNPCDialogueComponent::ResetActiveSelection()
{
	ActiveOfferedCandidates.Reset();
	ActiveCandidateExclusions.Reset();
}

FName ULLMNPCDialogueComponent::ResolveSkeletonProfileId() const
{
	if (MotionComponent)
	{
		if (const ULLMNPCSkeletonProfile* Profile = MotionComponent->SkeletonProfile.LoadSynchronous())
		{
			return Profile->ProfileId;
		}
	}
	return TEXT("ue5_manny.v1");
}
