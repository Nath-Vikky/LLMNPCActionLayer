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
#include "Protocol/LLMNPCProtocolCompatibility.h"
#include "Protocol/LLMNPCTurnRequestV3Adapter.h"
#include "Providers/LLMNPCModelProviderRegistry.h"
#include "Selection/LLMNPCCandidateRetriever.h"
#include "Selection/LLMNPCSelectionAnalyticsSubsystem.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Style/LLMNPCStyleResolver.h"
#include "Templates/LLMNPCTemplateCandidate.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"
#include "TimerManager.h"
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
	ClearRequestWatchdog();
	if (BehaviorCoordinator)
	{
		BehaviorCoordinator->Shutdown();
	}
	FallbackModelProvider.Reset();
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
	LastProviderResult = FLLMNPCModelTurnResult();
	LastSourceCandidateCount = 0;
	LastOfferedCandidateCount = 0;
	LastExcludedCandidateCount = 0;
	LastRequestSchemaVersion.Reset();

	EnsureRuntimeObjects();
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const FString PromptVersion = Settings
		? Settings->SelectionPromptVersion
		: FLLMNPCProtocolCompatibility::CurrentSelectionPrompt();
	if (!FLLMNPCProtocolCompatibility::IsSupportedSelectionPrompt(PromptVersion))
	{
		CompleteFailure(
			TEXT("LLMNPC_SELECTION_PROMPT_VERSION_UNSUPPORTED"),
			PromptVersion,
			false
		);
		return false;
	}
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
	LastSourceCandidateCount = SourceCandidates.Num();

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
	LastOfferedCandidateCount = Retrieval.Candidates.Num();
	LastExcludedCandidateCount = Retrieval.Exclusions.Num();
	ActiveOfferedCandidates = MoveTemp(Retrieval.Candidates);
	ActiveCandidateExclusions = MoveTemp(Retrieval.Exclusions);
	const FLLMNPCProviderCapabilityProfile ProviderCapabilities =
		ModelProvider->GetCapabilityProfile();
	LastRequestSchemaVersion =
		ProviderCapabilities.SupportsTurnRequestSchema(
			FLLMNPCProtocolCompatibility::CurrentTurnRequestSchema()
		)
		? FLLMNPCProtocolCompatibility::CurrentTurnRequestSchema()
		: ProviderCapabilities.PreferredTurnRequestSchema;
	if (!FLLMNPCProtocolCompatibility::IsSupportedTurnRequestSchema(
		LastRequestSchemaVersion
	))
	{
		CompleteFailure(
			TEXT("LLMNPC_PROVIDER_TURN_REQUEST_SCHEMA_UNSUPPORTED"),
			LastRequestSchemaVersion,
			false
		);
		return false;
	}
	TArray<FName> AdapterExclusions;
	TArray<FLLMNPCTemplateCandidate> AdaptedCandidates;
	FLLMNPCTurnRequestV3Adapter::AdaptCandidatesForSchema(
		LastRequestSchemaVersion,
		ActiveOfferedCandidates,
		AdaptedCandidates,
		&AdapterExclusions
	);
	for (const FName ExcludedId : AdapterExclusions)
	{
		FLLMNPCCandidateExclusion& Exclusion =
			ActiveCandidateExclusions.AddDefaulted_GetRef();
		Exclusion.SelectionId = ExcludedId;
		Exclusion.Reason = TEXT("request_schema_adapter");
	}
	ActiveOfferedCandidates = MoveTemp(AdaptedCandidates);
	LastOfferedCandidateCount = ActiveOfferedCandidates.Num();
	LastExcludedCandidateCount = ActiveCandidateExclusions.Num();
	LastOfferedCandidates = ActiveOfferedCandidates;

	ActiveRequest = FLLMNPCModelTurnRequest();
	ActiveRequest.RequestId = FGuid::NewGuid();
	ActiveRequest.SessionId = ConversationSession->GetSessionId();
	ActiveRequest.NPCId = NPCId;
	ActiveRequest.UserMessage = CleanMessage;
	ActiveRequest.ContextJson = ConversationSession->BuildContextualRequestJsonForSchema(
		ActiveRequest.RequestId,
		ActiveOfferedCandidates,
		RetrievalRequest.Context,
		PromptVersion,
		LastRequestSchemaVersion
	);
	if (GetWorld() && GetWorld()->GetGameInstance())
	{
		SelectionAnalytics = GetWorld()->GetGameInstance()->GetSubsystem<ULLMNPCSelectionAnalyticsSubsystem>();
	}
	if (SelectionAnalytics && Settings && Settings->bEnableLocalSelectionTelemetry)
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
	ArmRequestWatchdog();

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
	ClearRequestWatchdog();
	if (ModelProvider)
	{
		ModelProvider->CancelRequest(CancelledRequestId);
	}
	if (FallbackModelProvider)
	{
		FallbackModelProvider->CancelRequest(CancelledRequestId);
		FallbackModelProvider.Reset();
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
	LastProviderResult = FLLMNPCModelTurnResult();
	LastSourceCandidateCount = 0;
	LastOfferedCandidateCount = 0;
	LastExcludedCandidateCount = 0;
	SetState(ELLMNPCDialogueState::Idle);
}

void ULLMNPCDialogueComponent::SetProviderKind(ELLMNPCModelProviderKind NewProviderKind)
{
	if (ProviderKind == NewProviderKind && ProviderIdOverride.IsNone())
	{
		return;
	}
	CancelActiveRequest();
	ProviderKind = NewProviderKind;
	ProviderIdOverride = NAME_None;
	RecreateProvider();
}

void ULLMNPCDialogueComponent::SetProviderId(FName NewProviderId)
{
	if (ProviderIdOverride == NewProviderId)
	{
		return;
	}
	CancelActiveRequest();
	ProviderIdOverride = NewProviderId;
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
	Debug.LastRequestId = LastTurnResult.RequestId;
	Debug.ProviderId =
		!bRequestInFlight && !LastTurnResult.ProviderId.IsNone()
			? LastTurnResult.ProviderId
			: LastProviderId;
	Debug.ProviderModelId = LastTurnResult.ProviderModelId;
	Debug.LastSelectedActionId = LastTurnResult.SelectedActionId;
	Debug.LastResolvedTemplateId = LastTurnResult.ResolvedTemplateId;
	Debug.LastErrorCode = LastTurnResult.ErrorCode;
	Debug.LastErrorMessage = LastTurnResult.ErrorMessage;
	Debug.MessageCount = ConversationSession ? ConversationSession->GetMessages().Num() : 0;
	Debug.SourceCandidateCount = LastSourceCandidateCount;
	Debug.OfferedCandidateCount = LastOfferedCandidateCount;
	Debug.ExcludedCandidateCount = LastExcludedCandidateCount;
	Debug.RequestSchemaVersion = LastRequestSchemaVersion;
	Debug.bUsedLocalFallback = LastTurnResult.bUsedLocalFallback;
	const FLLMNPCSelectionContextSnapshot Context = GetSelectionContextSnapshot();
	Debug.ContextSummary = FString::Printf(
		TEXT("emotion=%s:%.2f personality=%s targets=%d states=%d"),
		*Context.Emotion.PrimaryEmotion.ToString(),
		Context.Emotion.Intensity,
		*Context.Personality.ProfileId.ToString(),
		Context.AvailableTargets.Num(),
		Context.ActiveStates.Num()
	);
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
	FallbackModelProvider.Reset();
	ModelProvider.Reset();
	ModelProvider = FLLMNPCModelProviderRegistry::Get().CreateProvider(ResolveProviderId());
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

FName ULLMNPCDialogueComponent::ResolveProviderId() const
{
	if (!ProviderIdOverride.IsNone())
	{
		return ProviderIdOverride;
	}

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	if (
		ProviderKind == ELLMNPCModelProviderKind::UseProjectSettings &&
		Settings &&
		!Settings->DefaultProviderId.IsNone()
	)
	{
		return Settings->DefaultProviderId;
	}

	switch (ResolveProviderKind())
	{
	case ELLMNPCModelProviderKind::BackendProxy:
		return TEXT("backend_proxy");
	case ELLMNPCModelProviderKind::DeepSeekDirectEditorOnly:
		return TEXT("deepseek_direct_editor");
	case ELLMNPCModelProviderKind::Mock:
	case ELLMNPCModelProviderKind::UseProjectSettings:
	default:
		return TEXT("mock");
	}
}

void ULLMNPCDialogueComponent::ArmRequestWatchdog()
{
	ClearRequestWatchdog();
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const float TimeoutSeconds = Settings
		? Settings->DialogueRequestWatchdogSeconds
		: 30.0f;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RequestWatchdogHandle,
			this,
			&ULLMNPCDialogueComponent::HandleRequestWatchdog,
			FMath::Max(TimeoutSeconds, 2.0f),
			false
		);
	}
}

void ULLMNPCDialogueComponent::ClearRequestWatchdog()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RequestWatchdogHandle);
	}
	RequestWatchdogHandle.Invalidate();
}

void ULLMNPCDialogueComponent::HandleRequestWatchdog()
{
	if (!bRequestInFlight)
	{
		return;
	}

	const FGuid TimedOutRequestId = ActiveRequest.RequestId;
	const bool bFallbackTimedOut = FallbackModelProvider.IsValid();
	bRequestInFlight = false;
	if (ModelProvider)
	{
		ModelProvider->CancelRequest(TimedOutRequestId);
	}
	if (FallbackModelProvider)
	{
		FallbackModelProvider->CancelRequest(TimedOutRequestId);
		FallbackModelProvider.Reset();
	}
	bRequestInFlight = true;

	FLLMNPCModelTurnResult TimeoutResult;
	TimeoutResult.RequestId = TimedOutRequestId;
	TimeoutResult.ProviderId = LastProviderId;
	TimeoutResult.ErrorCode = TEXT("LLMNPC_PROVIDER_WATCHDOG_TIMEOUT");
	TimeoutResult.ErrorMessage = TEXT("The model provider did not complete before the dialogue watchdog expired.");
	HandleProviderFailure(TimeoutResult, bFallbackTimedOut);
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
	if (
		(bUsedFallback && !FallbackModelProvider.IsValid()) ||
		(!bUsedFallback && FallbackModelProvider.IsValid())
	)
	{
		return;
	}

	LastProviderResult = ProviderResult;
	ClearRequestWatchdog();
	LastProviderId = ProviderResult.ProviderId;
	SetState(ELLMNPCDialogueState::Receiving);
	if (!ProviderResult.bSuccess)
	{
		HandleProviderFailure(ProviderResult, bUsedFallback);
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
	const FLLMNPCModelTurnResult& ProviderResult,
	bool bFallbackAlreadyUsed
)
{
	LastProviderResult = ProviderResult;
	if (
		!bFallbackAlreadyUsed &&
		bEnableLocalCommandFallback &&
		ResolveProviderId() != FName(TEXT("mock"))
	)
	{
		FLLMNPCModelTurnRequest FallbackRequest = ActiveRequest;
		FallbackRequest.bFallbackRequest = true;
		const TWeakObjectPtr<ULLMNPCDialogueComponent> WeakThis(this);
		FallbackModelProvider = FLLMNPCModelProviderRegistry::Get().CreateProvider(TEXT("mock"));
		if (!FallbackModelProvider)
		{
			CompleteFailure(
				TEXT("LLMNPC_FALLBACK_PROVIDER_UNAVAILABLE"),
				TEXT("The local fallback provider is not registered."),
				true
			);
			return;
		}
		ArmRequestWatchdog();
		FallbackModelProvider->SendTurn(
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
	LastTurnResult.ProviderId = LastProviderResult.ProviderId;
	LastTurnResult.ProviderModelId = LastProviderResult.ProviderModelId;
	LastTurnResult.HttpStatus = LastProviderResult.HttpStatus;
	LastTurnResult.AttemptCount = LastProviderResult.AttemptCount;
	LastTurnResult.TotalLatencySeconds = LastProviderResult.TotalLatencySeconds;
	LastTurnResult.PromptTokens = LastProviderResult.PromptTokens;
	LastTurnResult.CompletionTokens = LastProviderResult.CompletionTokens;
	LastTurnResult.TotalTokens = LastProviderResult.TotalTokens;
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
		ClearRequestWatchdog();
		FallbackModelProvider.Reset();
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
		ClearRequestWatchdog();
		FallbackModelProvider.Reset();
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
	ClearRequestWatchdog();
	FallbackModelProvider.Reset();
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
	LastTurnResult.ProviderId = LastProviderResult.ProviderId;
	LastTurnResult.ProviderModelId = LastProviderResult.ProviderModelId;
	LastTurnResult.HttpStatus = LastProviderResult.HttpStatus;
	LastTurnResult.AttemptCount = LastProviderResult.AttemptCount;
	LastTurnResult.TotalLatencySeconds = LastProviderResult.TotalLatencySeconds;
	LastTurnResult.PromptTokens = LastProviderResult.PromptTokens;
	LastTurnResult.CompletionTokens = LastProviderResult.CompletionTokens;
	LastTurnResult.TotalTokens = LastProviderResult.TotalTokens;
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
	ClearRequestWatchdog();
	FallbackModelProvider.Reset();
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
