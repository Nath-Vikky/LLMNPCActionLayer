#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Context/LLMNPCEmotionComponent.h"
#include "Context/LLMNPCPersonalityProfile.h"
#include "Context/LLMNPCRelationshipComponent.h"
#include "Context/LLMNPCSceneContextComponent.h"
#include "Dialogue/LLMNPCConversationSession.h"
#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "Providers/LLMNPCMockProvider.h"
#include "Selection/LLMNPCCandidateRetriever.h"
#include "Selection/LLMNPCSelectionAnalyticsSubsystem.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

namespace
{
constexpr uint32 Phase4TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

FString MakeText(std::initializer_list<uint16> Characters)
{
	FString Result;
	for (const uint16 Character : Characters)
	{
		Result.AppendChar(Character);
	}
	return Result;
}

ULLMNPCTemplateLibrarySubsystem* MakeLibrary()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	ULLMNPCTemplateLibrarySubsystem* Library = NewObject<ULLMNPCTemplateLibrarySubsystem>(GameInstance);
	Library->RefreshLibrary();
	return Library;
}

const FLLMNPCTemplateCandidate* FindCandidate(
	const TArray<FLLMNPCTemplateCandidate>& Candidates,
	FName SelectionId
)
{
	return Candidates.FindByPredicate(
		[SelectionId](const FLLMNPCTemplateCandidate& Candidate)
		{
			return Candidate.SelectionId == SelectionId;
		}
	);
}

FLLMNPCModelTurnDecision RunMock(
	const FString& UserMessage,
	const FString& ContextJson,
	FAutomationTestBase& Test
)
{
	FLLMNPCModelTurnRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.UserMessage = UserMessage;
	Request.ContextJson = ContextJson;
	FLLMNPCModelTurnResult ProviderResult;
	FLLMNPCMockProvider Provider;
	Provider.SendTurn(
		Request,
		[&ProviderResult](const FLLMNPCModelTurnResult& Result)
		{
			ProviderResult = Result;
		}
	);

	FLLMNPCModelTurnDecision Decision;
	FString Error;
	Test.TestTrue(TEXT("Contextual Mock output parses"), FLLMNPCModelTurnParser::Parse(
		ProviderResult.ResponseJson,
		Decision,
		Error
	));
	return Decision;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase4ContextComponentsTest,
	"LLMNPCActionLayer.Phase4.Context.ComponentsAndSceneTargets",
	Phase4TestFlags
)

bool FLLMNPCPhase4ContextComponentsTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCEmotionComponent* Emotion = NewObject<ULLMNPCEmotionComponent>();
	Emotion->SetEmotion(TEXT("friendly"), 0.8f, 0.7f, 0.3f);
	TestEqual(TEXT("Emotion snapshots retain the semantic tag"), Emotion->GetEmotionSnapshot().PrimaryEmotion, FName(TEXT("friendly")));

	ULLMNPCRelationshipComponent* Relationship = NewObject<ULLMNPCRelationshipComponent>();
	Relationship->SetRelationship(TEXT("player"), 0.75f, 0.6f, 0.5f, { TEXT("friend") });
	TestEqual(TEXT("Relationship context retains familiarity"), Relationship->GetRelationshipSnapshot().Familiarity, 0.75f);

	ULLMNPCSceneContextComponent* Scene = NewObject<ULLMNPCSceneContextComponent>();
	AActor* Door = NewObject<AActor>();
	Scene->RegisterSceneTarget(TEXT("door.main"), Door, TEXT("door"), { TEXT("door"), TEXT("exit") }, 0.9f);
	Scene->SetStateActive(TEXT("right_hand_busy"), true);
	const FLLMNPCSelectionContextSnapshot Snapshot = Scene->AppendToSnapshot(FLLMNPCSelectionContextSnapshot());
	TestEqual(TEXT("A valid target enters the context"), Snapshot.AvailableTargets.Num(), 1);
	TestTrue(TEXT("Occupied body state enters the context"), Snapshot.ActiveStates.Contains(TEXT("right_hand_busy")));
	Scene->SetTargetAvailable(TEXT("door.main"), false);
	TestEqual(TEXT("Unavailable targets are hidden"), Scene->AppendToSnapshot(FLLMNPCSelectionContextSnapshot()).AvailableTargets.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase4CandidateRetrieverTest,
	"LLMNPCActionLayer.Phase4.Selection.ContextFilteringAndPersonality",
	Phase4TestFlags
)

bool FLLMNPCPhase4CandidateRetrieverTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateLibrarySubsystem* Library = MakeLibrary();
	TArray<FLLMNPCTemplateCandidate> SourceCandidates;
	Library->QueryRuntimeCandidates(TEXT("ue5_manny.v1"), SourceCandidates);
	TestEqual(TEXT("Phase 4 library exposes nod, wave, and point"), SourceCandidates.Num(), 3);

	FLLMNPCCandidateRetrievalRequest Request;
	Request.UserMessage = MakeText({ 0x4F60, 0x597D });
	Request.SourceCandidates = SourceCandidates;
	Request.NowSeconds = 100.0;
	Request.Context.Personality.ProfileId = TEXT("shy");
	Request.Context.Personality.Shyness = 1.0f;
	Request.Context.Personality.Expressiveness = 1.0f;
	Request.Context.ActiveStates.Add(TEXT("right_hand_busy"));
	FLLMNPCSceneTargetContext& Door = Request.Context.AvailableTargets.AddDefaulted_GetRef();
	Door.TargetRef = TEXT("door.main");
	Door.Category = TEXT("door");
	Door.SemanticTags = { TEXT("door"), TEXT("exit") };
	Door.Salience = 0.9f;

	FLLMNPCCandidateRetrievalResult Result = ULLMNPCCandidateRetriever::Retrieve(Request);
	TestNotNull(TEXT("A busy right hand keeps head-only nod"), FindCandidate(Result.Candidates, TEXT("gesture.nod")));
	const FLLMNPCTemplateCandidate* MirroredWave = FindCandidate(Result.Candidates, TEXT("gesture.wave.right"));
	TestNotNull(TEXT("A mirror-capable wave remains available"), MirroredWave);
	if (MirroredWave)
	{
		TestTrue(TEXT("The wave is marked for UE-side mirroring"), MirroredWave->bMirrorRecommended);
	}
	TestNull(TEXT("Non-mirrorable point remains blocked"), FindCandidate(Result.Candidates, TEXT("gesture.point.target")));

	Request.Context.ActiveStates.Reset();
	Result = ULLMNPCCandidateRetriever::Retrieve(Request);
	const FLLMNPCTemplateCandidate* ShyWave = FindCandidate(Result.Candidates, TEXT("gesture.wave.right"));
	TestNotNull(TEXT("Wave returns when the hand is free"), ShyWave);
	if (ShyWave)
	{
		TestTrue(TEXT("A shy profile narrows the allowed wave amplitude"), ShyWave->AmplitudeRange.Y < 1.0f);
	}

	Request.Context.AvailableTargets.Reset();
	Result = ULLMNPCCandidateRetriever::Retrieve(Request);
	TestNull(TEXT("Target-requiring point is removed without a legal target"), FindCandidate(Result.Candidates, TEXT("gesture.point.target")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase4HistoryAndPolicyTest,
	"LLMNPCActionLayer.Phase4.Selection.CooldownRepetitionAndPolicy",
	Phase4TestFlags
)

bool FLLMNPCPhase4HistoryAndPolicyTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateLibrarySubsystem* Library = MakeLibrary();
	FLLMNPCCandidateRetrievalRequest Request;
	Library->QueryRuntimeCandidates(TEXT("ue5_manny.v1"), Request.SourceCandidates);
	Request.UserMessage = TEXT("hello");
	Request.NowSeconds = 50.0;
	Request.RepeatSuppressionSeconds = 2.0f;
	FLLMNPCActionHistoryEntry& History = Request.ActionHistory.AddDefaulted_GetRef();
	History.SelectionId = TEXT("gesture.wave.right");
	History.TimestampSeconds = 49.5;
	FLLMNPCCandidateRetrievalResult Result = ULLMNPCCandidateRetriever::Retrieve(Request);
	TestNull(TEXT("A cooling-down wave is not offered"), FindCandidate(Result.Candidates, TEXT("gesture.wave.right")));
	TestTrue(TEXT("Cooldown exclusion remains observable"), Result.Exclusions.ContainsByPredicate(
		[](const FLLMNPCCandidateExclusion& Exclusion)
		{
			return Exclusion.SelectionId == TEXT("gesture.wave.right") && Exclusion.Reason == TEXT("cooldown");
		}
	));

	Request.ActionHistory.Reset();
	Request.Context.Personality.Shyness = 1.0f;
	Result = ULLMNPCCandidateRetriever::Retrieve(Request);
	const FLLMNPCTemplateCandidate* Wave = FindCandidate(Result.Candidates, TEXT("gesture.wave.right"));
	TestNotNull(TEXT("The shy Wave remains available"), Wave);
	if (!Wave)
	{
		return false;
	}
	TestEqual(
		TEXT("The shy Wave resolves its context-safe style"),
		Wave->RecommendedStyle,
		FName(TEXT("subtle"))
	);
	TestTrue(
		TEXT("The friendly style remains available when it can honor the shy bound"),
		Wave->AllowedStyles.Contains(TEXT("friendly"))
	);
	FLLMNPCModelTurnDecision Decision;
	Decision.Action.Decision = TEXT("execute_template");
	Decision.Action.TemplateId = TEXT("gesture.wave.right");
	Decision.Action.Style = TEXT("friendly");
	Decision.Action.Amplitude = 99.0f;
	FString Error;
	TestTrue(TEXT("An offered action passes contextual policy"), ULLMNPCCandidateRetriever::ApplySelectionPolicy(Decision, Result.Candidates, Error));
	TestEqual(
		TEXT("UE clamps model amplitude to the shy bound"),
		Decision.Action.Amplitude,
		static_cast<float>(Wave->AmplitudeRange.Y)
	);

	Decision.Action.TemplateId = TEXT("gesture.not_offered");
	TestFalse(TEXT("A model cannot bypass the offered set"), ULLMNPCCandidateRetriever::ApplySelectionPolicy(Decision, Result.Candidates, Error));
	TestEqual(TEXT("Offered-set rejection has a stable code"), Error, FString(TEXT("LLMNPC_SELECTION_ACTION_NOT_OFFERED")));

	FLLMNPCSceneTargetContext& Door = Request.Context.AvailableTargets.AddDefaulted_GetRef();
	Door.TargetRef = TEXT("door.main");
	Door.Category = TEXT("door");
	Door.Salience = 1.0f;
	Result = ULLMNPCCandidateRetriever::Retrieve(Request);
	Decision.Action.TemplateId = TEXT("gesture.point.target");
	Decision.Action.Style = TEXT("neutral");
	Decision.Action.TargetRef = TEXT("door.not_registered");
	TestFalse(TEXT("A model cannot invent a Target Ref"), ULLMNPCCandidateRetriever::ApplySelectionPolicy(Decision, Result.Candidates, Error));
	TestEqual(TEXT("Target-set rejection has a stable code"), Error, FString(TEXT("LLMNPC_SELECTION_TARGET_NOT_OFFERED")));
	Decision.Action.TargetRef.Reset();
	TestTrue(TEXT("UE may bind the candidate's legal default Target"), ULLMNPCCandidateRetriever::ApplySelectionPolicy(Decision, Result.Candidates, Error));
	TestEqual(TEXT("The legal default Target is applied"), Decision.Action.TargetRef, FString(TEXT("door.main")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase4AutonomousAcceptanceTest,
	"LLMNPCActionLayer.Phase4.Integration.AutonomousSelectionExamples",
	Phase4TestFlags
)

bool FLLMNPCPhase4AutonomousAcceptanceTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateLibrarySubsystem* Library = MakeLibrary();
	TArray<FLLMNPCTemplateCandidate> SourceCandidates;
	Library->QueryRuntimeCandidates(TEXT("ue5_manny.v1"), SourceCandidates);
	ULLMNPCConversationSession* Session = NewObject<ULLMNPCConversationSession>();
	Session->InitializeSession(TEXT("phase4_npc"));

	FLLMNPCSelectionContextSnapshot Context;
	Context.Emotion.PrimaryEmotion = TEXT("friendly");
	Context.Emotion.Intensity = 0.8f;
	FLLMNPCSceneTargetContext& Door = Context.AvailableTargets.AddDefaulted_GetRef();
	Door.TargetRef = TEXT("door.main");
	Door.Category = TEXT("door");
	Door.SemanticTags = { TEXT("door"), TEXT("exit") };
	Door.Salience = 0.95f;

	auto Retrieve = [&SourceCandidates, &Context](const FString& Message)
	{
		FLLMNPCCandidateRetrievalRequest Request;
		Request.UserMessage = Message;
		Request.SourceCandidates = SourceCandidates;
		Request.Context = Context;
		Request.NowSeconds = 100.0;
		return ULLMNPCCandidateRetriever::Retrieve(Request).Candidates;
	};

	const FString Greeting = MakeText({ 0x4F60, 0x597D });
	TArray<FLLMNPCTemplateCandidate> Offered = Retrieve(Greeting);
	Session->AddActionHistory(
		TEXT("gesture.nod"),
		TEXT("gesture.nod.manny.v1"),
		FString(),
		TEXT("agreement")
	);
	FString ContextJson = Session->BuildContextualRequestJson(FGuid::NewGuid(), Offered, Context, TEXT("llmnpc.selection_prompt.v1"));
	TestTrue(TEXT("The v2 request carries its prompt version"), ContextJson.Contains(TEXT("llmnpc.selection_prompt.v1")));
	TestTrue(TEXT("The request carries structured personality context"), ContextJson.Contains(TEXT("\"personality\"")));
	TestFalse(TEXT("Action history does not expose internal template IDs"), ContextJson.Contains(TEXT("gesture.nod.manny.v1")));
	FLLMNPCModelTurnDecision Decision = RunMock(Greeting, ContextJson, *this);
	TestEqual(TEXT("A friendly greeting selects wave"), Decision.Action.TemplateId, FName(TEXT("gesture.wave.right")));

	const FString AgreementQuestion = MakeText({ 0x4F60, 0x540C, 0x610F, 0x5417 });
	Offered = Retrieve(AgreementQuestion);
	ContextJson = Session->BuildContextualRequestJson(FGuid::NewGuid(), Offered, Context, TEXT("llmnpc.selection_prompt.v1"));
	Decision = RunMock(AgreementQuestion, ContextJson, *this);
	TestEqual(TEXT("An agreement question selects nod"), Decision.Action.TemplateId, FName(TEXT("gesture.nod")));

	const FString DoorQuestion = MakeText({ 0x95E8, 0x5728, 0x54EA, 0x91CC });
	Offered = Retrieve(DoorQuestion);
	ContextJson = Session->BuildContextualRequestJson(FGuid::NewGuid(), Offered, Context, TEXT("llmnpc.selection_prompt.v1"));
	Decision = RunMock(DoorQuestion, ContextJson, *this);
	TestEqual(TEXT("A where question selects gaze and point"), Decision.Action.TemplateId, FName(TEXT("gesture.point.target")));
	TestEqual(TEXT("The point action uses the legal door target"), Decision.Action.TargetRef, FString(TEXT("door.main")));

	Context.ActiveStates.Add(TEXT("right_hand_busy"));
	Offered = Retrieve(Greeting);
	ContextJson = Session->BuildContextualRequestJson(FGuid::NewGuid(), Offered, Context, TEXT("llmnpc.selection_prompt.v1"));
	Decision = RunMock(Greeting, ContextJson, *this);
	TestEqual(TEXT("A busy right hand retains the mirror-capable wave"), Decision.Action.TemplateId, FName(TEXT("gesture.wave.right")));
	FString PolicyError;
	TestTrue(TEXT("The mirrored wave passes contextual policy"), ULLMNPCCandidateRetriever::ApplySelectionPolicy(Decision, Offered, PolicyError));
	TestTrue(TEXT("UE marks the selected wave for left-hand execution"), Decision.Action.bMirror);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase4TargetTemplateTest,
	"LLMNPCActionLayer.Phase4.Templates.TargetPointCompilation",
	Phase4TestFlags
)

bool FLLMNPCPhase4TargetTemplateTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateLibrarySubsystem* Library = MakeLibrary();
	const ULLMNPCMotionTemplate* MotionTemplate = Library->ResolveRuntimeModelTemplate(
		TEXT("gesture.point.target"),
		TEXT("ue5_manny.v1")
	);
	const ULLMNPCSkeletonProfile* Profile = Library->FindSkeletonProfile(TEXT("ue5_manny.v1"));
	TestNotNull(TEXT("The Published target point template resolves"), MotionTemplate);
	TestNotNull(TEXT("The Manny profile resolves"), Profile);
	if (!MotionTemplate || !Profile)
	{
		return false;
	}

	FLLMNPCTemplateModifiers Modifiers;
	Modifiers.TargetRef = TEXT("door.main");
	Modifiers.Style = TEXT("neutral");
	FLLMMotionPlan Plan;
	FString Error;
	TestTrue(TEXT("Target point compiles through the template boundary"), FLLMNPCTemplateCompiler::Compile(
		*MotionTemplate,
		Modifiers,
		*Profile,
		Plan,
		Error
	));
	for (const FLLMMotionTrack& Track : Plan.Clip.Tracks)
	{
		if (
			Track.TrackType == ELLMMotionTrackType::IKReach ||
			Track.TrackType == ELLMMotionTrackType::LookAt
		)
		{
			TestEqual(TEXT("Every target-aware track receives the validated ref"), Track.TargetRef, FString(TEXT("door.main")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase4AnalyticsTest,
	"LLMNPCActionLayer.Phase4.Analytics.SelectionLifecycle",
	Phase4TestFlags
)

bool FLLMNPCPhase4AnalyticsTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	ULLMNPCSelectionAnalyticsSubsystem* Analytics = NewObject<ULLMNPCSelectionAnalyticsSubsystem>(GameInstance);
	const FGuid RequestId = FGuid::NewGuid();
	Analytics->BeginSelection(
		RequestId,
		TEXT("npc"),
		TEXT("mock"),
		TEXT("llmnpc.selection_prompt.v1"),
		{ TEXT("gesture.wave.right"), TEXT("gesture.nod") },
		1,
		TEXT("{\"context\":true}")
	);
	Analytics->CompleteSelection(
		RequestId,
		TEXT("gesture.wave.right"),
		TEXT("gesture.wave.right.manny.procedural.v1"),
		TEXT("executed"),
		NAME_None,
		false
	);
	TestEqual(TEXT("One selection event is retained"), Analytics->GetRecentEvents().Num(), 1);
	TestEqual(TEXT("Analytics retains the outcome"), Analytics->GetRecentEvents()[0].Outcome, FName(TEXT("executed")));
	TestEqual(TEXT("Analytics retains offered candidates"), Analytics->GetRecentEvents()[0].OfferedSelectionIds.Num(), 2);
	return true;
}

#endif
