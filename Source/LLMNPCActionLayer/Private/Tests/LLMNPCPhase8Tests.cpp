#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dialogue/LLMNPCDialogueComponent.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCMotionValidator.h"
#include "LLMNPCSettings.h"
#include "Protocol/LLMNPCProtocolCompatibility.h"
#include "Providers/LLMNPCModelProviderRegistry.h"

namespace
{
constexpr uint32 Phase8TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

class FLLMNPCPhase8TestProvider final : public ILLMNPCModelProvider
{
public:
	virtual void SendTurn(
		const FLLMNPCModelTurnRequest& Request,
		FLLMNPCModelTurnCallback Callback
	) override
	{
		FLLMNPCModelTurnResult Result;
		Result.RequestId = Request.RequestId;
		Result.ProviderId = GetProviderId();
		Result.bSuccess = true;
		Callback(Result);
	}

	virtual void CancelRequest(const FGuid& RequestId) override
	{
		static_cast<void>(RequestId);
	}

	virtual FName GetProviderId() const override
	{
		return TEXT("phase8_test");
	}
};

class FLLMNPCPhase8HangingProvider final : public ILLMNPCModelProvider
{
public:
	virtual void SendTurn(
		const FLLMNPCModelTurnRequest& Request,
		FLLMNPCModelTurnCallback InCallback
	) override
	{
		ActiveRequestId = Request.RequestId;
		Callback = MoveTemp(InCallback);
	}

	virtual void CancelRequest(const FGuid& RequestId) override
	{
		if (RequestId != ActiveRequestId || !Callback)
		{
			return;
		}
		FLLMNPCModelTurnResult Result;
		Result.RequestId = RequestId;
		Result.ProviderId = GetProviderId();
		Result.ErrorCode = TEXT("LLMNPC_PROVIDER_CANCELLED");
		FLLMNPCModelTurnCallback CancelCallback = MoveTemp(Callback);
		ActiveRequestId.Invalidate();
		CancelCallback(Result);
	}

	virtual FName GetProviderId() const override
	{
		return TEXT("phase8_hanging");
	}

private:
	FGuid ActiveRequestId;
	FLLMNPCModelTurnCallback Callback;
};

FLLMMotionPlan MakeVersionedPlan(const FString& Version)
{
	FLLMMotionPlan Plan;
	Plan.Version = Version;
	Plan.Clip.ClipId = TEXT("phase8_version_test");
	Plan.Clip.Duration = 0.5f;
	FLLMMotionTrack& Track = Plan.Clip.Tracks.AddDefaulted_GetRef();
	Track.ControlId = TEXT("head.pitch");
	Track.TrackType = ELLMMotionTrackType::Hold;
	Track.StartTime = 0.0f;
	Track.EndTime = 0.5f;
	Track.Amplitude = 5.0f;
	return Plan;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase8ProviderRegistryTest,
	"LLMNPCActionLayer.Phase8.Productization.ProviderRegistry",
	Phase8TestFlags
)

bool FLLMNPCPhase8ProviderRegistryTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCModelProviderRegistry& Registry = FLLMNPCModelProviderRegistry::Get();
	const FName ProviderId(TEXT("phase8_test"));
	Registry.UnregisterProvider(ProviderId);

	const FLLMNPCModelProviderFactory Factory =
		FLLMNPCModelProviderFactory::CreateLambda(
			[]() -> TSharedPtr<ILLMNPCModelProvider>
			{
				return MakeShared<FLLMNPCPhase8TestProvider>();
			}
		);
	TestTrue(TEXT("An external module can register a provider factory"), Registry.RegisterProvider(ProviderId, Factory));
	TestFalse(TEXT("Provider IDs cannot be replaced accidentally"), Registry.RegisterProvider(ProviderId, Factory));

	const TSharedPtr<ILLMNPCModelProvider> Provider = Registry.CreateProvider(ProviderId);
	TestTrue(TEXT("The registered provider can be constructed"), Provider.IsValid());
	TestEqual(TEXT("The factory preserves its public provider ID"), Provider->GetProviderId(), ProviderId);
	TestTrue(TEXT("Registered IDs are discoverable"), Registry.GetRegisteredProviderIds().Contains(ProviderId));
	TestTrue(TEXT("A provider can unregister during module shutdown"), Registry.UnregisterProvider(ProviderId));
	TestFalse(TEXT("Unregistered providers fail closed"), Registry.CreateProvider(ProviderId).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase8WatchdogRecoveryTest,
	"LLMNPCActionLayer.Phase8.Productization.WatchdogRecovery",
	Phase8TestFlags
)

bool FLLMNPCPhase8WatchdogRecoveryTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCModelProviderRegistry& Registry = FLLMNPCModelProviderRegistry::Get();
	const FName ProviderId(TEXT("phase8_hanging"));
	Registry.UnregisterProvider(ProviderId);
	const TSharedPtr<FLLMNPCPhase8HangingProvider> HangingProvider =
		MakeShared<FLLMNPCPhase8HangingProvider>();
	const FLLMNPCModelProviderFactory Factory =
		FLLMNPCModelProviderFactory::CreateLambda(
			[HangingProvider]() -> TSharedPtr<ILLMNPCModelProvider>
			{
				return HangingProvider;
			}
		);
	TestTrue(TEXT("The hanging test provider registers"), Registry.RegisterProvider(ProviderId, Factory));

	ULLMNPCDialogueComponent* Dialogue = NewObject<ULLMNPCDialogueComponent>();
	Dialogue->bEnableLocalCommandFallback = false;
	Dialogue->SetProviderId(ProviderId);
	TestTrue(TEXT("The hanging request starts"), Dialogue->SendPlayerMessage(TEXT("wave")));
	TestTrue(TEXT("The request is in flight before recovery"), Dialogue->IsRequestInFlight());

	Dialogue->HandleRequestWatchdog();
	TestFalse(TEXT("Watchdog recovery clears the in-flight state"), Dialogue->IsRequestInFlight());
	TestEqual(
		TEXT("Synchronous cancel callbacks cannot replace the watchdog result"),
		Dialogue->LastTurnResult.ErrorCode,
		FName(TEXT("LLMNPC_PROVIDER_WATCHDOG_TIMEOUT"))
	);
	TestEqual(
		TEXT("The component reaches a recoverable failed state"),
		Dialogue->GetDialogueState(),
		ELLMNPCDialogueState::Failed
	);
	Registry.UnregisterProvider(ProviderId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase8ProtocolCompatibilityTest,
	"LLMNPCActionLayer.Phase8.Productization.ProtocolCompatibility",
	Phase8TestFlags
)

bool FLLMNPCPhase8ProtocolCompatibilityTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	TestTrue(
		TEXT("The current prompt is supported"),
		FLLMNPCProtocolCompatibility::IsSupportedSelectionPrompt(
			FLLMNPCProtocolCompatibility::CurrentSelectionPrompt()
		)
	);
	TestFalse(
		TEXT("Unknown future response schemas fail closed"),
		FLLMNPCProtocolCompatibility::IsSupportedModelTurnSchema(TEXT("llmnpc.model_turn.v99"))
	);

	ULLMNPCMotionValidator* Validator = NewObject<ULLMNPCMotionValidator>();
	FLLMMotionPlan LegacyPlan = MakeVersionedPlan(TEXT("1.0.0"));
	const FLLMMotionValidationResult Migrated = Validator->ValidateAndClamp(
		LegacyPlan,
		ELLMNPCMotionValidationSource::PublishedTemplate
	);
	TestTrue(TEXT("A known legacy MotionPlan version migrates"), Migrated.bValid);
	TestEqual(
		TEXT("Legacy MotionPlans normalize to the current wire version"),
		LegacyPlan.Version,
		FLLMNPCProtocolCompatibility::CurrentMotionPlanVersion()
	);

	FLLMMotionPlan FuturePlan = MakeVersionedPlan(TEXT("2.0"));
	const FLLMMotionValidationResult Rejected = Validator->ValidateAndClamp(
		FuturePlan,
		ELLMNPCMotionValidationSource::PublishedTemplate
	);
	TestFalse(TEXT("Unknown MotionPlan major versions are rejected"), Rejected.bValid);
	TestTrue(
		TEXT("The rejection exposes a stable compatibility error"),
		Rejected.ErrorMessage.StartsWith(TEXT("LLMNPC_MOTION_PLAN_VERSION_UNSUPPORTED"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase8MotionLODTest,
	"LLMNPCActionLayer.Phase8.Productization.MotionLOD",
	Phase8TestFlags
)

bool FLLMNPCPhase8MotionLODTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	TestEqual(
		TEXT("Near NPCs receive full-rate sampling"),
		ULLMNPCMotionComponent::ResolveLODLevelForDistance(500.0f, 2000.0f, 6000.0f),
		ELLMNPCMotionLODLevel::Full
	);
	TestEqual(
		TEXT("Mid-distance NPCs use reduced-rate sampling"),
		ULLMNPCMotionComponent::ResolveLODLevelForDistance(4000.0f, 2000.0f, 6000.0f),
		ELLMNPCMotionLODLevel::Reduced
	);
	TestEqual(
		TEXT("Distant NPCs use minimal-rate sampling"),
		ULLMNPCMotionComponent::ResolveLODLevelForDistance(9000.0f, 2000.0f, 6000.0f),
		ELLMNPCMotionLODLevel::Minimal
	);
	TestEqual(
		TEXT("Misordered thresholds are normalized safely"),
		ULLMNPCMotionComponent::ResolveLODLevelForDistance(2500.0f, 3000.0f, 1000.0f),
		ELLMNPCMotionLODLevel::Full
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase8ReplicationBoundaryTest,
	"LLMNPCActionLayer.Phase8.Productization.ReplicationBoundary",
	Phase8TestFlags
)

bool FLLMNPCPhase8ReplicationBoundaryTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const UScriptStruct* CommandStruct = FLLMNPCReplicatedMotionCommand::StaticStruct();
	TestNotNull(
		TEXT("Replication carries the constrained procedural plan"),
		CommandStruct->FindPropertyByName(TEXT("ProceduralPlan"))
	);
	TestNotNull(
		TEXT("Replication carries semantic template IDs"),
		CommandStruct->FindPropertyByName(TEXT("TemplateId"))
	);
	TestNull(
		TEXT("Replication never carries evaluated bone pose snapshots"),
		CommandStruct->FindPropertyByName(TEXT("Snapshot"))
	);

	ULLMNPCMotionComponent* Component = NewObject<ULLMNPCMotionComponent>();
	TestTrue(TEXT("Motion components opt into UE replication by default"), Component->GetIsReplicated());
	TestTrue(
		TEXT("Local selection telemetry is privacy-opt-in"),
		!GetDefault<ULLMNPCSettings>()->bEnableLocalSelectionTelemetry
	);
	return true;
}

#endif
