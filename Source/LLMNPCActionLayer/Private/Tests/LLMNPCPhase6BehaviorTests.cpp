#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Behavior/LLMNPCBehaviorPlanValidator.h"
#include "Context/LLMNPCSceneContextComponent.h"
#include "Dialogue/LLMNPCConversationSession.h"
#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Providers/LLMNPCMockProvider.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

namespace
{
constexpr uint32 Phase6BehaviorTestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

FLLMNPCModelTurnDecision MakeMoveDecision(const FString& TargetRef, float AcceptanceRadiusCm)
{
	FLLMNPCModelTurnDecision Decision;
	Decision.SchemaVersion = TEXT("llmnpc.model_turn.v1");
	Decision.AssistantText = TEXT("I am on my way.");
	Decision.Action.Decision = TEXT("none");
	Decision.Locomotion.Decision = TEXT("move_to");
	Decision.Locomotion.TargetRef = TargetRef;
	Decision.Locomotion.AcceptanceRadiusCm = AcceptanceRadiusCm;
	return Decision;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase6BehaviorPlanTest,
	"LLMNPCActionLayer.Phase6.Behavior.RestrictedPlanBuilder",
	Phase6BehaviorTestFlags
)

bool FLLMNPCPhase6BehaviorPlanTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCSceneContextComponent* SceneContext = NewObject<ULLMNPCSceneContextComponent>();
	AActor* Owner = NewObject<AActor>();
	AActor* PlayerTarget = NewObject<AActor>();
	SceneContext->RegisterSceneTarget(
		TEXT("player.main"),
		PlayerTarget,
		TEXT("player"),
		{ TEXT("conversation_partner") },
		1.0f
	);

	FLLMNPCBehaviorPolicy Policy;
	Policy.MinAcceptanceRadiusCm = 50.0f;
	Policy.MaxAcceptanceRadiusCm = 500.0f;
	Policy.DefaultAcceptanceRadiusCm = 150.0f;
	FLLMNPCBehaviorPlan Plan;
	FString Error;
	TestTrue(
		TEXT("A registered high-level movement target builds a behavior plan"),
		FLLMNPCBehaviorPlanValidator::BuildPlan(
			MakeMoveDecision(TEXT("player.main"), 900.0f),
			nullptr,
			FLLMNPCTemplateModifiers(),
			SceneContext,
			Owner,
			Policy,
			Plan,
			Error
		)
	);
	TestEqual(TEXT("Move and face are the only generated navigation steps"), Plan.Steps.Num(), 2);
	if (Plan.Steps.Num() == 2)
	{
		TestEqual(TEXT("Movement is first"), Plan.Steps[0].Kind, ELLMNPCBehaviorStepKind::MoveToTarget);
		TestEqual(TEXT("Target facing follows movement"), Plan.Steps[1].Kind, ELLMNPCBehaviorStepKind::FaceTarget);
		TestEqual(TEXT("The radius is clamped by UE policy"), Plan.Steps[0].AcceptanceRadiusCm, 500.0f);
	}

	TestFalse(
		TEXT("An invented target cannot enter a plan"),
		FLLMNPCBehaviorPlanValidator::BuildPlan(
			MakeMoveDecision(TEXT("player.invented"), 150.0f),
			nullptr,
			FLLMNPCTemplateModifiers(),
			SceneContext,
			Owner,
			Policy,
			Plan,
			Error
		)
	);
	TestEqual(TEXT("Invented targets have a stable error"), Error, FString(TEXT("LLMNPC_BEHAVIOR_TARGET_NOT_AVAILABLE")));

	SceneContext->RegisterSceneTarget(TEXT("self"), Owner, TEXT("npc"), {}, 1.0f);
	TestFalse(
		TEXT("An NPC cannot navigate to itself"),
		FLLMNPCBehaviorPlanValidator::BuildPlan(
			MakeMoveDecision(TEXT("self"), 150.0f),
			nullptr,
			FLLMNPCTemplateModifiers(),
			SceneContext,
			Owner,
			Policy,
			Plan,
			Error
		)
	);
	TestEqual(TEXT("Self targets have a stable error"), Error, FString(TEXT("LLMNPC_BEHAVIOR_TARGET_IS_SELF")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase6BehaviorPlanOrderTest,
	"LLMNPCActionLayer.Phase6.Behavior.PlanOrderValidation",
	Phase6BehaviorTestFlags
)

bool FLLMNPCPhase6BehaviorPlanOrderTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCBehaviorPolicy Policy;
	FLLMNPCBehaviorPlan Plan;
	Plan.PlanId = FGuid::NewGuid();
	Plan.TimeoutSeconds = 20.0f;
	FLLMNPCBehaviorStep& Face = Plan.Steps.AddDefaulted_GetRef();
	Face.Kind = ELLMNPCBehaviorStepKind::FaceTarget;
	Face.TargetRef = TEXT("player.main");
	Face.TimeoutSeconds = 2.0f;
	FLLMNPCBehaviorStep& Move = Plan.Steps.AddDefaulted_GetRef();
	Move.Kind = ELLMNPCBehaviorStepKind::MoveToTarget;
	Move.TargetRef = TEXT("player.main");
	Move.AcceptanceRadiusCm = 150.0f;
	Move.TimeoutSeconds = 15.0f;

	FString Error;
	TestFalse(TEXT("Facing before movement is rejected"), FLLMNPCBehaviorPlanValidator::ValidatePlan(Plan, Policy, Error));
	TestEqual(TEXT("Invalid ordering has a stable error"), Error, FString(TEXT("LLMNPC_BEHAVIOR_FACE_ORDER_INVALID")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase6LocomotionContractTest,
	"LLMNPCActionLayer.Phase6.Behavior.ModelTurnContract",
	Phase6BehaviorTestFlags
)

bool FLLMNPCPhase6LocomotionContractTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const FString MoveResponse = TEXT(R"JSON(
{
  "schema_version": "llmnpc.model_turn.v1",
  "assistant_text": "I am on my way.",
  "action": {
    "decision": "none",
    "template_id": "",
    "target_ref": "",
    "amplitude": 1.0,
    "speed_scale": 1.0,
    "duration_scale": 1.0,
    "style": "neutral",
    "reason_tag": "locomotion_request"
  },
  "locomotion": {
    "decision": "move_to",
    "target_ref": "player.main",
    "acceptance_radius_cm": 99999.0
  }
}
)JSON");

	FLLMNPCModelTurnDecision Decision;
	FString Error;
	TestTrue(TEXT("The strict parser accepts the bounded move_to contract"), FLLMNPCModelTurnParser::Parse(MoveResponse, Decision, Error));

	UGameInstance* TestGameInstance = NewObject<UGameInstance>();
	ULLMNPCTemplateLibrarySubsystem* Library = NewObject<ULLMNPCTemplateLibrarySubsystem>(TestGameInstance);
	Library->RefreshLibrary();
	const ULLMNPCMotionTemplate* MotionTemplate = nullptr;
	FLLMNPCTemplateModifiers Modifiers;
	TestTrue(
		TEXT("Business validation accepts movement without requiring an action template"),
		FLLMNPCModelTurnValidator::ValidateAndResolve(
			Decision,
			*Library,
			TEXT("ue5_manny.v1"),
			MotionTemplate,
			Modifiers,
			Error
		)
	);
	TestNull(TEXT("Movement does not resolve a hidden animation asset"), MotionTemplate);
	TestEqual(TEXT("The model radius is clamped by project policy"), Decision.Locomotion.AcceptanceRadiusCm, 500.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase6MockLocomotionTest,
	"LLMNPCActionLayer.Phase6.Behavior.MockProviderSceneTarget",
	Phase6BehaviorTestFlags
)

bool FLLMNPCPhase6MockLocomotionTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCConversationSession* Session = NewObject<ULLMNPCConversationSession>();
	Session->InitializeSession(TEXT("test_npc"), 4);
	Session->AddMessage(ELLMNPCDialogueRole::Player, TEXT("come here"));
	FLLMNPCSelectionContextSnapshot Context;
	FLLMNPCSceneTargetContext& Player = Context.AvailableTargets.AddDefaulted_GetRef();
	Player.TargetRef = TEXT("player.main");
	Player.Category = TEXT("player");
	Player.Salience = 1.0f;

	FLLMNPCModelTurnRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.UserMessage = TEXT("come here");
	Request.ContextJson = Session->BuildContextualRequestJson(
		Request.RequestId,
		{},
		Context,
		TEXT("llmnpc.selection_prompt.v3")
	);

	FLLMNPCMockProvider Mock;
	FLLMNPCModelTurnResult ProviderResult;
	Mock.SendTurn(Request, [&ProviderResult](const FLLMNPCModelTurnResult& Result) { ProviderResult = Result; });
	FLLMNPCModelTurnDecision Decision;
	FString Error;
	TestTrue(TEXT("Mock movement output remains schema-valid"), FLLMNPCModelTurnParser::Parse(ProviderResult.ResponseJson, Decision, Error));
	TestEqual(TEXT("Mock emits only the high-level move_to intent"), Decision.Locomotion.Decision, FName(TEXT("move_to")));
	TestEqual(TEXT("Mock selects a target from scene context"), Decision.Locomotion.TargetRef, FString(TEXT("player.main")));
	TestEqual(TEXT("Mock does not invent an action"), Decision.Action.Decision, FName(TEXT("none")));
	return true;
}

#endif
