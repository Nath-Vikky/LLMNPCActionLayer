#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dialogue/LLMNPCConversationSession.h"
#include "Dialogue/LLMNPCDialogueComponent.h"
#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Engine/GameInstance.h"
#include "LLMNPCSettings.h"
#include "LLMNPCMotionSampler.h"
#include "Providers/LLMNPCBackendProxyProvider.h"
#include "Providers/LLMNPCDeepSeekProvider.h"
#include "Providers/LLMNPCMockProvider.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"
#include "UI/LLMNPCChatWidget.h"

namespace
{
constexpr uint32 Phase2TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const FString ValidNodResponse = TEXT(R"JSON(
{
  "schema_version": "llmnpc.model_turn.v1",
  "assistant_text": "Yes.",
  "action": {
    "decision": "execute_template",
    "template_id": "gesture.nod",
    "target_ref": "",
    "amplitude": 1.0,
    "speed_scale": 1.0,
    "duration_scale": 1.0,
    "style": "neutral",
    "reason_tag": "agreement"
  },
  "locomotion": {
    "decision": "none",
    "target_ref": "",
    "acceptance_radius_cm": 0.0
  }
}
)JSON");

FString MakeChineseCommand(uint16 First, uint16 Second)
{
	FString Result;
	Result.AppendChar(First);
	Result.AppendChar(Second);
	return Result;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCModelTurnParserTest,
	"LLMNPCActionLayer.Phase2.ModelTurn.StrictParser",
	Phase2TestFlags
)

bool FLLMNPCModelTurnParserTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCModelTurnDecision Decision;
	FString Error;
	TestTrue(TEXT("A valid Model Turn parses"), FLLMNPCModelTurnParser::Parse(ValidNodResponse, Decision, Error));
	TestEqual(TEXT("The parser keeps assistant text"), Decision.AssistantText, FString(TEXT("Yes.")));
	TestEqual(TEXT("The parser keeps the public action ID"), Decision.Action.TemplateId, FName(TEXT("gesture.nod")));

	FString OldSchema = ValidNodResponse.Replace(
		TEXT("llmnpc.model_turn.v1"),
		TEXT("llmnpc.model_turn.v0")
	);
	TestFalse(TEXT("An old schema is rejected"), FLLMNPCModelTurnParser::Parse(OldSchema, Decision, Error));
	TestEqual(TEXT("Old schema has a stable reason"), Error, FString(TEXT("LLMNPC_MODEL_SCHEMA_UNSUPPORTED")));

	FString UnknownField = ValidNodResponse.Replace(
		TEXT("\"reason_tag\": \"agreement\""),
		TEXT("\"reason_tag\": \"agreement\", \"tracks\": []")
	);
	TestFalse(TEXT("Raw track fields are rejected"), FLLMNPCModelTurnParser::Parse(UnknownField, Decision, Error));
	TestTrue(TEXT("Unknown action fields identify the boundary"), Error.Contains(TEXT("LLMNPC_MODEL_ACTION_FIELD_UNKNOWN")));

	const FString MissingModifier = ValidNodResponse.Replace(TEXT("    \"amplitude\": 1.0,\n"), TEXT(""));
	TestFalse(TEXT("Schema-required modifiers cannot be omitted"), FLLMNPCModelTurnParser::Parse(MissingModifier, Decision, Error));
	TestEqual(TEXT("Missing modifiers have a stable reason"), Error, FString(TEXT("LLMNPC_MODEL_ACTION_MODIFIER_MISSING")));

	const FString NegativeRadius = ValidNodResponse.Replace(TEXT("\"acceptance_radius_cm\": 0.0"), TEXT("\"acceptance_radius_cm\": -1.0"));
	TestFalse(TEXT("Negative locomotion radii are rejected"), FLLMNPCModelTurnParser::Parse(NegativeRadius, Decision, Error));
	TestEqual(TEXT("Negative radius rejection is stable"), Error, FString(TEXT("LLMNPC_MODEL_LOCOMOTION_RADIUS_NEGATIVE")));

	FString AssetPath = ValidNodResponse.Replace(
		TEXT("gesture.nod"),
		TEXT("/Game/Animations/Nod")
	);
	TestFalse(TEXT("Asset paths cannot be template IDs"), FLLMNPCModelTurnParser::Parse(AssetPath, Decision, Error));
	TestEqual(TEXT("Asset path rejection is stable"), Error, FString(TEXT("LLMNPC_MODEL_TEMPLATE_ID_PATH_FORBIDDEN")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCProviderResponseTest,
	"LLMNPCActionLayer.Phase2.Providers.ResponseContracts",
	Phase2TestFlags
)

bool FLLMNPCProviderResponseTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCMockProvider Mock;
	FLLMNPCModelTurnRequest NodRequest;
	NodRequest.RequestId = FGuid::NewGuid();
	NodRequest.UserMessage = MakeChineseCommand(0x70B9, 0x5934);
	bool bMockCalled = false;
	Mock.SendTurn(
		NodRequest,
		[this, &bMockCalled](const FLLMNPCModelTurnResult& Result)
		{
			bMockCalled = true;
			FLLMNPCModelTurnDecision Decision;
			FString Error;
			TestTrue(TEXT("Chinese nod mock response parses"), FLLMNPCModelTurnParser::Parse(Result.ResponseJson, Decision, Error));
			TestEqual(TEXT("Chinese nod selects the public nod action"), Decision.Action.TemplateId, FName(TEXT("gesture.nod")));
		}
	);
	TestTrue(TEXT("Mock callbacks are deterministic"), bMockCalled);

	NodRequest.UserMessage = MakeChineseCommand(0x6325, 0x624B);
	Mock.SendTurn(
		NodRequest,
		[this](const FLLMNPCModelTurnResult& Result)
		{
			FLLMNPCModelTurnDecision Decision;
			FString ParseError;
			TestTrue(TEXT("Chinese wave mock response parses"), FLLMNPCModelTurnParser::Parse(Result.ResponseJson, Decision, ParseError));
			TestEqual(TEXT("Chinese wave selects the public wave action"), Decision.Action.TemplateId, FName(TEXT("gesture.wave.right")));
		}
	);

	NodRequest.UserMessage = MakeChineseCommand(0x4F60, 0x597D);
	Mock.SendTurn(
		NodRequest,
		[this](const FLLMNPCModelTurnResult& Result)
		{
			FLLMNPCModelTurnDecision Decision;
			FString ParseError;
			TestTrue(TEXT("Chinese greeting mock response parses"), FLLMNPCModelTurnParser::Parse(Result.ResponseJson, Decision, ParseError));
			TestEqual(TEXT("The Phase 2 greeting selects the optional wave"), Decision.Action.TemplateId, FName(TEXT("gesture.wave.right")));
		}
	);

	FString Extracted;
	FString Error;
	const FString BackendEnvelope = FString::Printf(
		TEXT("{\"request_id\":\"test\",\"decision\":%s}"),
		*ValidNodResponse
	);
	TestTrue(
		TEXT("Backend envelopes expose the decision only"),
		FLLMNPCBackendProxyProvider::ExtractDecisionJson(BackendEnvelope, Extracted, Error)
	);
	FLLMNPCModelTurnDecision BackendDecision;
	TestTrue(
		TEXT("Extracted backend decision is parseable"),
		FLLMNPCModelTurnParser::Parse(Extracted, BackendDecision, Error)
	);

	FString EscapedDecision = ValidNodResponse;
	EscapedDecision.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	EscapedDecision.ReplaceInline(TEXT("\""), TEXT("\\\""));
	EscapedDecision.ReplaceInline(TEXT("\r"), TEXT(""));
	EscapedDecision.ReplaceInline(TEXT("\n"), TEXT("\\n"));
	const FString DeepSeekEnvelope = FString::Printf(
		TEXT("{\"choices\":[{\"finish_reason\":\"stop\",\"message\":{\"content\":\"%s\"}}]}"),
		*EscapedDecision
	);
	TestTrue(
		TEXT("DeepSeek chat content is extracted"),
		FLLMNPCDeepSeekProvider::ExtractDecisionJson(DeepSeekEnvelope, Extracted, Error)
	);
	FLLMNPCModelTurnDecision ExtractedDecision;
	TestTrue(TEXT("Extracted DeepSeek decision is parseable"), FLLMNPCModelTurnParser::Parse(Extracted, ExtractedDecision, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCCandidateBoundaryTest,
	"LLMNPCActionLayer.Phase2.Templates.ModelCandidateBoundary",
	Phase2TestFlags
)

bool FLLMNPCCandidateBoundaryTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	UGameInstance* TestGameInstance = NewObject<UGameInstance>();
	ULLMNPCTemplateLibrarySubsystem* Library =
		NewObject<ULLMNPCTemplateLibrarySubsystem>(TestGameInstance);
	Library->RefreshLibrary();

	TArray<FLLMNPCTemplateCandidate> Candidates;
	Library->QueryRuntimeCandidates(TEXT("ue5_manny.v1"), Candidates);
	TestEqual(TEXT("Manny exposes eight public actions"), Candidates.Num(), 8);
	TestTrue(
		TEXT("The Published Beckon candidate is model-visible"),
		Candidates.ContainsByPredicate(
			[](const FLLMNPCTemplateCandidate& Candidate)
			{
				return Candidate.SelectionId == TEXT("gesture.beckon");
			}
		)
	);
	TestTrue(
		TEXT("The Published Present candidate is model-visible"),
		Candidates.ContainsByPredicate(
			[](const FLLMNPCTemplateCandidate& Candidate)
			{
				return Candidate.SelectionId == TEXT("gesture.present");
			}
		)
	);
	TestTrue(
		TEXT("The Published Thumbs-Up candidate is model-visible"),
		Candidates.ContainsByPredicate(
			[](const FLLMNPCTemplateCandidate& Candidate)
			{
				return Candidate.SelectionId == TEXT("gesture.thumbs_up");
			}
		)
	);
	for (const FLLMNPCTemplateCandidate& Candidate : Candidates)
	{
		TestFalse(TEXT("Candidate selection IDs are skeleton independent"), Candidate.SelectionId.ToString().Contains(TEXT(".manny.")));
	}
	TestNull(
		TEXT("Internal template IDs remain unavailable to the model"),
		Library->ResolveRuntimeModelTemplate(TEXT("gesture.wave.right.manny.fk.v1"), TEXT("ue5_manny.v1"))
	);

	ULLMNPCConversationSession* Session = NewObject<ULLMNPCConversationSession>();
	Session->InitializeSession(TEXT("test_npc"), 12);
	Session->AddMessage(ELLMNPCDialogueRole::Player, TEXT("wave"));
	const FString ContextJson = Session->BuildRequestContextJson(FGuid::NewGuid(), Candidates);
	TestTrue(TEXT("Context includes the public wave action"), ContextJson.Contains(TEXT("gesture.wave.right")));
	TestTrue(TEXT("Context includes the public Clap action"), ContextJson.Contains(TEXT("gesture.clap")));
	TestTrue(TEXT("Context includes the public Shrug action"), ContextJson.Contains(TEXT("gesture.shrug")));
	TestTrue(TEXT("Context includes the public Beckon action"), ContextJson.Contains(TEXT("gesture.beckon")));
	TestTrue(TEXT("Context includes the public Present action"), ContextJson.Contains(TEXT("gesture.present")));
	TestTrue(TEXT("Context includes the public Thumbs-Up action"), ContextJson.Contains(TEXT("gesture.thumbs_up")));
	TestFalse(TEXT("Context does not expose the faithful internal variant"), ContextJson.Contains(TEXT(".fk.v1")));
	TestFalse(TEXT("Context does not expose the Clap implementation ID"), ContextJson.Contains(TEXT("gesture.clap.manny.asset.v1")));
	TestFalse(TEXT("Context does not expose the Shrug implementation ID"), ContextJson.Contains(TEXT("gesture.shrug.manny.generated")));
	TestFalse(TEXT("Context does not expose the Beckon implementation ID"), ContextJson.Contains(TEXT("gesture.beckon.manny.procedural.generated")));
	TestFalse(TEXT("Context does not expose the Present implementation ID"), ContextJson.Contains(TEXT("gesture.present.manny.procedural.generated")));
	TestFalse(TEXT("Context does not expose the Thumbs-Up implementation ID"), ContextJson.Contains(TEXT("gesture.thumbs_up.manny.procedural.generated")));
	TestFalse(TEXT("Context does not expose raw controls"), ContextJson.Contains(TEXT("right_upperarm")));

	FLLMNPCModelTurnDecision UnknownDecision;
	FString Error;
	const FString UnknownResponse = ValidNodResponse.Replace(TEXT("gesture.nod"), TEXT("gesture.not_published"));
	TestTrue(TEXT("An unknown but well-formed ID reaches business validation"), FLLMNPCModelTurnParser::Parse(UnknownResponse, UnknownDecision, Error));
	const ULLMNPCMotionTemplate* UnknownTemplate = nullptr;
	FLLMNPCTemplateModifiers UnknownModifiers;
	TestFalse(
		TEXT("An unpublished public ID is rejected"),
		FLLMNPCModelTurnValidator::ValidateAndResolve(
			UnknownDecision,
			*Library,
			TEXT("ue5_manny.v1"),
			UnknownTemplate,
			UnknownModifiers,
			Error
		)
	);
	TestNull(TEXT("The rejected ID never resolves an executable template"), UnknownTemplate);
	TestEqual(TEXT("Assistant text remains available after business rejection"), UnknownDecision.AssistantText, FString(TEXT("Yes.")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCCommandPipelineTest,
	"LLMNPCActionLayer.Phase2.Integration.MockToPublishedTemplateSnapshot",
	Phase2TestFlags
)

bool FLLMNPCCommandPipelineTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	UGameInstance* TestGameInstance = NewObject<UGameInstance>();
	ULLMNPCTemplateLibrarySubsystem* Library =
		NewObject<ULLMNPCTemplateLibrarySubsystem>(TestGameInstance);
	Library->RefreshLibrary();
	const ULLMNPCSkeletonProfile* Profile = Library->FindSkeletonProfile(TEXT("ue5_manny.v1"));
	TestNotNull(TEXT("The pipeline resolves the Manny profile"), Profile);
	if (!Profile)
	{
		return false;
	}

	FLLMNPCMockProvider Mock;
	FLLMNPCModelTurnRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.UserMessage = TEXT("nod");
	FLLMNPCModelTurnResult ProviderResult;
	Mock.SendTurn(Request, [&ProviderResult](const FLLMNPCModelTurnResult& Result) { ProviderResult = Result; });

	FLLMNPCModelTurnDecision Decision;
	FString Error;
	TestTrue(TEXT("Mock output parses"), FLLMNPCModelTurnParser::Parse(ProviderResult.ResponseJson, Decision, Error));
	Decision.Action.Amplitude = 99.0f;
	const ULLMNPCMotionTemplate* MotionTemplate = nullptr;
	FLLMNPCTemplateModifiers Modifiers;
	TestTrue(
		TEXT("Model selection resolves through the business validator"),
		FLLMNPCModelTurnValidator::ValidateAndResolve(
			Decision,
			*Library,
			Profile->ProfileId,
			MotionTemplate,
			Modifiers,
			Error
		)
	);
	TestNotNull(TEXT("The public nod resolves to a Published template"), MotionTemplate);
	TestEqual(TEXT("Out-of-range amplitude is clamped by policy"), Modifiers.Amplitude, 1.2f);
	if (!MotionTemplate)
	{
		return false;
	}

	FLLMMotionPlan Plan;
	TestTrue(
		TEXT("The selected Published template compiles"),
		FLLMNPCTemplateCompiler::Compile(*MotionTemplate, Modifiers, *Profile, Plan, Error)
	);
	FLLMProceduralPoseSnapshot Snapshot;
	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	FLLMNPCMotionSampler::SampleClip(Plan.Clip, nullptr, nullptr, EmptyTargets, 0.14625f, Snapshot);
	TestTrue(TEXT("The end-to-end command produces a nod pose"), Snapshot.HeadPitch > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCDialogueSessionTest,
	"LLMNPCActionLayer.Phase2.Dialogue.MultiTurnMock",
	Phase2TestFlags
)

bool FLLMNPCDialogueSessionTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCDialogueComponent* Dialogue = NewObject<ULLMNPCDialogueComponent>();
	Dialogue->ProviderKind = ELLMNPCModelProviderKind::Mock;
	TestTrue(TEXT("First Mock turn is accepted"), Dialogue->SendPlayerMessage(TEXT("status report")));
	TestTrue(TEXT("Second Mock turn is accepted"), Dialogue->SendPlayerMessage(TEXT("anything else")));
	TestEqual(TEXT("Two turns retain four messages"), Dialogue->GetConversationSession()->GetMessages().Num(), 4);
	TestTrue(
		*FString::Printf(
			TEXT("No-action turns have no behavior error: %s"),
			*Dialogue->LastTurnResult.ErrorCode.ToString()
		),
		Dialogue->LastTurnResult.ErrorCode.IsNone()
	);
	TestEqual(TEXT("Synchronous Mock returns to Idle"), Dialogue->GetDialogueState(), ELLMNPCDialogueState::Idle);
	TestTrue(TEXT("The latest turn has displayable text"), Dialogue->LastTurnResult.bTextResponseReceived);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCChatWidgetAssetTest,
	"LLMNPCActionLayer.Phase2.UI.DefaultChatWidgetAsset",
	Phase2TestFlags
)

bool FLLMNPCChatWidgetAssetTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	TestNotNull(TEXT("Project settings are available"), Settings);
	if (!Settings)
	{
		return false;
	}

	UClass* WidgetClass = Settings->DefaultChatWidgetClass.LoadSynchronous();
	TestNotNull(TEXT("The default chat widget asset loads"), WidgetClass);
	if (WidgetClass)
	{
		TestTrue(
			TEXT("The WBP derives from the native chat widget"),
			WidgetClass->IsChildOf(ULLMNPCChatWidget::StaticClass())
		);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCDialogueLocalFallbackTest,
	"LLMNPCActionLayer.Phase2.Dialogue.DirectProviderLocalFallback",
	Phase2TestFlags
)

bool FLLMNPCDialogueLocalFallbackTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCSettings* Settings = GetMutableDefault<ULLMNPCSettings>();
	const bool bPreviousDirectCallSetting = Settings->bAllowDirectProviderCallInEditorOnly;
	Settings->bAllowDirectProviderCallInEditorOnly = false;

	ULLMNPCDialogueComponent* Dialogue = NewObject<ULLMNPCDialogueComponent>();
	Dialogue->ProviderKind = ELLMNPCModelProviderKind::DeepSeekDirectEditorOnly;
	Dialogue->bEnableLocalCommandFallback = true;
	TestTrue(TEXT("The remote turn request is accepted"), Dialogue->SendPlayerMessage(TEXT("status report")));
	TestTrue(TEXT("A disabled direct provider uses the local fallback"), Dialogue->LastTurnResult.bUsedLocalFallback);
	TestTrue(TEXT("The fallback returns displayable text"), Dialogue->LastTurnResult.bTextResponseReceived);
	TestEqual(TEXT("The fallback completes without blocking the session"), Dialogue->GetDialogueState(), ELLMNPCDialogueState::Idle);
	TestEqual(
		TEXT("The original provider failure remains observable"),
		Dialogue->LastTurnResult.ErrorCode,
		FName(TEXT("LLMNPC_DEEPSEEK_DIRECT_DISABLED"))
	);

	Settings->bAllowDirectProviderCallInEditorOnly = bPreviousDirectCallSetting;
	return true;
}

#endif
