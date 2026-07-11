#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dialogue/LLMNPCConversationSession.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Actor.h"
#include "LLMNPCMotionSampler.h"
#include "MicroMotion/LLMNPCMicroMotionScheduler.h"
#include "Selection/LLMNPCCandidateRetriever.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Style/LLMNPCStyleResolver.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"

namespace
{
constexpr uint32 Phase5TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

ULLMNPCTemplateLibrarySubsystem* MakePhase5Library()
{
	UGameInstance* GameInstance = NewObject<UGameInstance>();
	ULLMNPCTemplateLibrarySubsystem* Library = NewObject<ULLMNPCTemplateLibrarySubsystem>(GameInstance);
	Library->RefreshLibrary();
	return Library;
}

const FLLMNPCTemplateCandidate* FindPhase5Candidate(
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

const FLLMMotionTrack* FindTrack(const FLLMMotionPlan& Plan, FName ControlId)
{
	return Plan.Clip.Tracks.FindByPredicate(
		[ControlId](const FLLMMotionTrack& Track)
		{
			return Track.ControlId == ControlId;
		}
	);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase5StyleResolverTest,
	"LLMNPCActionLayer.Phase5.Style.ContextMappingAndSeed",
	Phase5TestFlags
)

bool FLLMNPCPhase5StyleResolverTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const TArray<FName> Styles = { TEXT("neutral"), TEXT("friendly"), TEXT("subtle"), TEXT("excited") };
	FLLMNPCSelectionContextSnapshot Context;
	Context.Emotion.PrimaryEmotion = TEXT("excited");
	Context.Emotion.Intensity = 0.8f;
	TestEqual(TEXT("Excited emotion maps to excited style"), ULLMNPCStyleResolver::ResolveRecommendedStyle(Context, Styles), FName(TEXT("excited")));

	Context.Emotion.PrimaryEmotion = TEXT("neutral");
	Context.Emotion.Intensity = 0.0f;
	Context.Personality.Shyness = 0.9f;
	TestEqual(TEXT("Shy personality maps to subtle style"), ULLMNPCStyleResolver::ResolveRecommendedStyle(Context, Styles), FName(TEXT("subtle")));

	const FGuid SessionId(1, 2, 3, 4);
	const FGuid RequestId(5, 6, 7, 8);
	const int32 SeedA = ULLMNPCStyleResolver::BuildDeterministicSeed(SessionId, RequestId, TEXT("npc"), TEXT("gesture.wave.right"));
	const int32 SeedB = ULLMNPCStyleResolver::BuildDeterministicSeed(SessionId, RequestId, TEXT("npc"), TEXT("gesture.wave.right"));
	const int32 SeedC = ULLMNPCStyleResolver::BuildDeterministicSeed(SessionId, FGuid(5, 6, 7, 9), TEXT("npc"), TEXT("gesture.wave.right"));
	TestEqual(TEXT("The same selection identity produces the same seed"), SeedA, SeedB);
	TestNotEqual(TEXT("A different request changes the seed"), SeedA, SeedC);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase5VariantAndCurveTest,
	"LLMNPCActionLayer.Phase5.Templates.VariantAndDeterministicCurves",
	Phase5TestFlags
)

bool FLLMNPCPhase5VariantAndCurveTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateLibrarySubsystem* Library = MakePhase5Library();
	const ULLMNPCMotionTemplate* SubtleVariant = Library->ResolvePublishedVariant(
		TEXT("gesture.wave.right"),
		TEXT("ue5_manny.v1"),
		TEXT("subtle"),
		12345
	);
	TestNotNull(TEXT("Subtle style resolves a Published variant"), SubtleVariant);
	if (SubtleVariant)
	{
		TestEqual(TEXT("The style-specific variant wins over generic variants"), SubtleVariant->Metadata.VariantId, FName(TEXT("subtle")));
	}

	const ULLMNPCMotionTemplate* DefaultVariant = Library->ResolvePublishedVariant(
		TEXT("gesture.wave.right"),
		TEXT("ue5_manny.v1"),
		TEXT("friendly"),
		12345
	);
	const ULLMNPCSkeletonProfile* Profile = Library->FindSkeletonProfile(TEXT("ue5_manny.v1"));
	TestNotNull(TEXT("Friendly style resolves the generic wave"), DefaultVariant);
	TestNotNull(TEXT("Manny profile resolves"), Profile);
	if (!DefaultVariant || !Profile)
	{
		return false;
	}

	FLLMNPCTemplateModifiers Modifiers;
	Modifiers.Style = TEXT("friendly");
	Modifiers.RandomSeed = 2468;
	Modifiers.ContextAmplitudeRange = FVector2D(0.75f, 0.75f);
	FLLMMotionPlan PlanA;
	FLLMMotionPlan PlanB;
	FString Error;
	TestTrue(TEXT("Seeded style compiles"), FLLMNPCTemplateCompiler::Compile(*DefaultVariant, Modifiers, *Profile, PlanA, Error));
	TestTrue(TEXT("The same seeded style compiles again"), FLLMNPCTemplateCompiler::Compile(*DefaultVariant, Modifiers, *Profile, PlanB, Error));
	const FLLMMotionTrack* WaveA = FindTrack(PlanA, TEXT("right_hand.local_offset.y"));
	const FLLMMotionTrack* WaveB = FindTrack(PlanB, TEXT("right_hand.local_offset.y"));
	TestNotNull(TEXT("Compiled wave keeps its bounded oscillator"), WaveA);
	TestNotNull(TEXT("Repeated compilation keeps the oscillator"), WaveB);
	if (WaveA && WaveB)
	{
		TestEqual(TEXT("Seed jitter cannot escape the contextual amplitude range"), WaveA->Amplitude, 12.0f);
		TestEqual(TEXT("Same seed reproduces frequency"), WaveA->Frequency, WaveB->Frequency);
		TestEqual(TEXT("Same seed reproduces phase"), WaveA->Phase, WaveB->Phase);
	}

	Modifiers.RandomSeed = 2469;
	FLLMMotionPlan PlanC;
	TestTrue(TEXT("A different seed still compiles"), FLLMNPCTemplateCompiler::Compile(*DefaultVariant, Modifiers, *Profile, PlanC, Error));
	const FLLMMotionTrack* WaveC = FindTrack(PlanC, TEXT("right_hand.local_offset.y"));
	if (WaveA && WaveC)
	{
		TestTrue(TEXT("Different seeds vary a bounded curve parameter"), !FMath::IsNearlyEqual(WaveA->Frequency, WaveC->Frequency) || !FMath::IsNearlyEqual(WaveA->Phase, WaveC->Phase));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase5MirrorTest,
	"LLMNPCActionLayer.Phase5.Templates.SemanticMirrorToLeftHand",
	Phase5TestFlags
)

bool FLLMNPCPhase5MirrorTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateLibrarySubsystem* Library = MakePhase5Library();
	const ULLMNPCMotionTemplate* Wave = Library->ResolvePublishedVariant(
		TEXT("gesture.wave.right"), TEXT("ue5_manny.v1"), TEXT("friendly"), 10
	);
	const ULLMNPCSkeletonProfile* Profile = Library->FindSkeletonProfile(TEXT("ue5_manny.v1"));
	if (!Wave || !Profile)
	{
		AddError(TEXT("Mirror prerequisites did not load."));
		return false;
	}

	FLLMNPCTemplateModifiers Modifiers;
	Modifiers.Style = TEXT("friendly");
	Modifiers.RandomSeed = 10;
	Modifiers.bMirror = true;
	FLLMMotionPlan Plan;
	FString Error;
	TestTrue(TEXT("Mirror-capable wave compiles"), FLLMNPCTemplateCompiler::Compile(*Wave, Modifiers, *Profile, Plan, Error));
	TestNotNull(TEXT("Right-hand IK becomes left-hand IK"), FindTrack(Plan, TEXT("left_hand.ik")));
	TestNotNull(TEXT("Right finger pose becomes left finger pose"), FindTrack(Plan, TEXT("left_fingers.open")));
	TestNull(TEXT("Mirrored plan contains no right-hand IK"), FindTrack(Plan, TEXT("right_hand.ik")));

	FLLMProceduralPoseSnapshot Snapshot;
	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	FLLMNPCMotionSampler::SampleClip(Plan.Clip, nullptr, nullptr, EmptyTargets, 0.6f, Snapshot);
	TestTrue(TEXT("Mirrored sampling activates left-hand IK"), Snapshot.LeftHandIKAlpha > 0.0f);
	TestEqual(TEXT("Mirrored sampling leaves right-hand IK inactive"), Snapshot.RightHandIKAlpha, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase5BusyHandSelectionTest,
	"LLMNPCActionLayer.Phase5.Selection.BusyRightHandUsesMirror",
	Phase5TestFlags
)

bool FLLMNPCPhase5BusyHandSelectionTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateLibrarySubsystem* Library = MakePhase5Library();
	FLLMNPCCandidateRetrievalRequest Request;
	Library->QueryRuntimeCandidates(TEXT("ue5_manny.v1"), Request.SourceCandidates);
	Request.UserMessage = TEXT("hello");
	Request.Context.ActiveStates.Add(TEXT("right_hand_busy"));
	Request.Context.Personality.Shyness = 0.8f;
	FLLMNPCCandidateRetrievalResult Result = ULLMNPCCandidateRetriever::Retrieve(Request);
	const FLLMNPCTemplateCandidate* Wave = FindPhase5Candidate(Result.Candidates, TEXT("gesture.wave.right"));
	TestNotNull(TEXT("Mirror-capable wave remains a candidate"), Wave);
	if (Wave)
	{
		TestTrue(TEXT("Candidate policy recommends mirroring"), Wave->bMirrorRecommended);
		TestEqual(TEXT("Shy context recommends subtle style"), Wave->RecommendedStyle, FName(TEXT("subtle")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase5MicroMotionTest,
	"LLMNPCActionLayer.Phase5.MicroMotion.LocalSchedulerAndGazeArbitration",
	Phase5TestFlags
)

bool FLLMNPCPhase5MicroMotionTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMNPCMicroMotionConfig Config;
	Config.GazeSwitchInterval = FVector2D(1.0f, 1.0f);
	TMap<FString, FVector> Targets;
	Targets.Add(TEXT("player"), FVector(100.0f, 20.0f, 80.0f));

	FLLMNPCMicroMotionState StateA;
	FLLMNPCMicroMotionState StateB;
	StateA.Initialize(99);
	StateB.Initialize(99);
	FLLMProceduralPoseSnapshot SnapshotA;
	FLLMProceduralPoseSnapshot SnapshotB;
	FLLMNPCMicroMotionScheduler::Update(Config, StateA, 0.25f, Targets, false, false, false, SnapshotA);
	FLLMNPCMicroMotionScheduler::Update(Config, StateB, 0.25f, Targets, false, false, false, SnapshotB);
	TestTrue(TEXT("Local scheduler adds breathing"), !FMath::IsNearlyZero(SnapshotA.ChestPitch));
	TestTrue(TEXT("Local scheduler adds ambient gaze"), SnapshotA.GazeAlpha > 0.0f);
	TestEqual(TEXT("Same seed reproduces gaze selection"), StateA.GazeTargetRef, StateB.GazeTargetRef);
	TestEqual(TEXT("Same seed reproduces chest motion"), SnapshotA.ChestPitch, SnapshotB.ChestPitch);

	FLLMNPCMicroMotionState BusyState;
	BusyState.Initialize(99);
	FLLMProceduralPoseSnapshot BusySnapshot;
	FLLMNPCMicroMotionScheduler::Update(Config, BusyState, 0.25f, Targets, true, true, true, BusySnapshot);
	TestEqual(TEXT("Formal chest channel suppresses breathing"), BusySnapshot.ChestPitch, 0.0f);
	TestEqual(TEXT("Formal gaze channel suppresses ambient gaze"), BusySnapshot.GazeAlpha, 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCPhase5RequestBoundaryTest,
	"LLMNPCActionLayer.Phase5.Security.NoRawCurveOrSeedAuthority",
	Phase5TestFlags
)

bool FLLMNPCPhase5RequestBoundaryTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	ULLMNPCTemplateLibrarySubsystem* Library = MakePhase5Library();
	TArray<FLLMNPCTemplateCandidate> Candidates;
	Library->QueryRuntimeCandidates(TEXT("ue5_manny.v1"), Candidates);
	ULLMNPCConversationSession* Session = NewObject<ULLMNPCConversationSession>();
	Session->InitializeSession(TEXT("phase5_npc"));
	const FString ContextJson = Session->BuildContextualRequestJson(
		FGuid::NewGuid(),
		Candidates,
		FLLMNPCSelectionContextSnapshot(),
		TEXT("llmnpc.selection_prompt.v2")
	);
	TestTrue(TEXT("Request exposes the versioned style recommendation"), ContextJson.Contains(TEXT("recommended_style")));
	TestTrue(TEXT("Request may expose UE mirror recommendation"), ContextJson.Contains(TEXT("mirror_recommended")));
	TestFalse(TEXT("Request does not expose random seed authority"), ContextJson.Contains(TEXT("random_seed")));
	TestFalse(TEXT("Request does not expose curve frequency"), ContextJson.Contains(TEXT("frequency")));
	TestFalse(TEXT("Request does not expose Keyframes"), ContextJson.Contains(TEXT("float_keys")));
	TestFalse(TEXT("Request does not expose semantic controls"), ContextJson.Contains(TEXT("right_hand.ik")));
	return true;
}

#endif
