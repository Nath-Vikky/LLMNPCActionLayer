#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Dialogue/LLMNPCConversationSession.h"
#include "Engine/GameInstance.h"
#include "Animation/Skeleton.h"
#include "GameFramework/Actor.h"
#include "LLMNPCMotionSampler.h"
#include "LLMNPCMotionMirror.h"
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

FTransform BuildReferenceComponentTransform(const FReferenceSkeleton& ReferenceSkeleton, int32 BoneIndex)
{
	const TArray<FTransform>& RefPose = ReferenceSkeleton.GetRefBonePose();
	FTransform Result = RefPose[BoneIndex];
	for (int32 ParentIndex = ReferenceSkeleton.GetParentIndex(BoneIndex);
		ParentIndex != INDEX_NONE;
		ParentIndex = ReferenceSkeleton.GetParentIndex(ParentIndex))
	{
		Result = Result * RefPose[ParentIndex];
	}
	return Result;
}

FVector EvaluateReferenceHandPosition(
	const USkeleton& Skeleton,
	bool bLeft,
	const FRotator& UpperDelta,
	const FRotator& LowerDelta,
	const FRotator& HandDelta
)
{
	const FReferenceSkeleton& ReferenceSkeleton = Skeleton.GetReferenceSkeleton();
	const int32 UpperIndex = ReferenceSkeleton.FindBoneIndex(bLeft ? TEXT("upperarm_l") : TEXT("upperarm_r"));
	const int32 LowerIndex = ReferenceSkeleton.FindBoneIndex(bLeft ? TEXT("lowerarm_l") : TEXT("lowerarm_r"));
	const int32 HandIndex = ReferenceSkeleton.FindBoneIndex(bLeft ? TEXT("hand_l") : TEXT("hand_r"));
	if (UpperIndex == INDEX_NONE || LowerIndex == INDEX_NONE || HandIndex == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}
	const int32 ParentIndex = ReferenceSkeleton.GetParentIndex(UpperIndex);
	if (ParentIndex == INDEX_NONE)
	{
		return FVector::ZeroVector;
	}

	const TArray<FTransform>& RefPose = ReferenceSkeleton.GetRefBonePose();
	FTransform UpperLocal = RefPose[UpperIndex];
	FTransform LowerLocal = RefPose[LowerIndex];
	FTransform HandLocal = RefPose[HandIndex];
	UpperLocal.SetRotation(FQuat(UpperDelta) * UpperLocal.GetRotation());
	LowerLocal.SetRotation(FQuat(LowerDelta) * LowerLocal.GetRotation());
	HandLocal.SetRotation(FQuat(HandDelta) * HandLocal.GetRotation());
	const FTransform UpperCS = UpperLocal * BuildReferenceComponentTransform(ReferenceSkeleton, ParentIndex);
	const FTransform LowerCS = LowerLocal * UpperCS;
	const FTransform HandCS = HandLocal * LowerCS;
	return HandCS.GetLocation();
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
		TestTrue(TEXT("Subtle Wave contains the reviewed FK upper-arm curve"), SubtleVariant->ProceduralClip.Tracks.ContainsByPredicate(
			[](const FLLMMotionTrack& Track)
			{
				return Track.ControlId == TEXT("right_upperarm.pitch");
			}
		));
		TestFalse(TEXT("Subtle Wave no longer uses the unreviewed hand-anchor IK"), SubtleVariant->ProceduralClip.Tracks.ContainsByPredicate(
			[](const FLLMMotionTrack& Track)
			{
				return Track.ControlId == TEXT("right_hand.ik");
			}
		));
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
	if (SubtleVariant)
	{
		FLLMNPCTemplateModifiers SubtleModifiers;
		SubtleModifiers.Style = TEXT("subtle");
		SubtleModifiers.Amplitude = 0.65f;
		SubtleModifiers.ContextAmplitudeRange = FVector2D(0.65f, 0.65f);
		FLLMMotionPlan SubtlePlan;
		FString SubtleError;
		TestTrue(TEXT("Subtle reviewed FK Wave compiles"), FLLMNPCTemplateCompiler::Compile(
			*SubtleVariant, SubtleModifiers, *Profile, SubtlePlan, SubtleError
		));
		if (const FLLMMotionTrack* Fingers = FindTrack(SubtlePlan, TEXT("right_fingers.open")))
		{
			TestEqual(TEXT("Gesture amplitude does not weaken normalized open-hand pose"), Fingers->FloatKeys[1].V, 1.0f);
		}
		else
		{
			AddError(TEXT("Subtle Wave lost its open-hand pose track."));
		}
	}
	TestEqual(
		TEXT("Friendly style resolves the manually reviewed Wave"),
		DefaultVariant->Metadata.TemplateId,
		FName(TEXT("gesture.wave.right.manny.fk.v1"))
	);

	FLLMNPCTemplateModifiers Modifiers;
	Modifiers.Style = TEXT("friendly");
	Modifiers.RandomSeed = 2468;
	Modifiers.ContextAmplitudeRange = FVector2D(0.75f, 0.75f);
	FLLMMotionPlan PlanA;
	FLLMMotionPlan PlanB;
	FString Error;
	TestTrue(TEXT("Seeded style compiles"), FLLMNPCTemplateCompiler::Compile(*DefaultVariant, Modifiers, *Profile, PlanA, Error));
	TestTrue(TEXT("The same seeded style compiles again"), FLLMNPCTemplateCompiler::Compile(*DefaultVariant, Modifiers, *Profile, PlanB, Error));
	const FLLMMotionTrack* WaveA = FindTrack(PlanA, TEXT("right_upperarm.pitch"));
	const FLLMMotionTrack* WaveB = FindTrack(PlanB, TEXT("right_upperarm.pitch"));
	TestNotNull(TEXT("Compiled Wave keeps its reviewed FK curve"), WaveA);
	TestNotNull(TEXT("Repeated compilation keeps the reviewed FK curve"), WaveB);
	if (WaveA && WaveB)
	{
		TestEqual(TEXT("Same seed reproduces FK key count"), WaveA->FloatKeys.Num(), WaveB->FloatKeys.Num());
		TestEqual(TEXT("Same seed reproduces FK value"), WaveA->FloatKeys[3].V, WaveB->FloatKeys[3].V);
		TestEqual(TEXT("Same seed reproduces FK timing"), WaveA->FloatKeys[3].T, WaveB->FloatKeys[3].T);
	}

	Modifiers.RandomSeed = 2469;
	FLLMMotionPlan PlanC;
	TestTrue(TEXT("A different seed still compiles"), FLLMNPCTemplateCompiler::Compile(*DefaultVariant, Modifiers, *Profile, PlanC, Error));
	const FLLMMotionTrack* WaveC = FindTrack(PlanC, TEXT("right_upperarm.pitch"));
	if (WaveA && WaveC)
	{
		TestTrue(TEXT("Different seeds vary bounded FK timing"), !FMath::IsNearlyEqual(WaveA->FloatKeys[3].T, WaveC->FloatKeys[3].T));
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
	FLLMMotionPlan RightPlan;
	FString Error;
	TestTrue(TEXT("Reviewed right Wave compiles"), FLLMNPCTemplateCompiler::Compile(*Wave, Modifiers, *Profile, RightPlan, Error));
	Modifiers.bMirror = true;
	FLLMMotionPlan Plan;
	TestTrue(TEXT("Mirror-capable wave compiles"), FLLMNPCTemplateCompiler::Compile(*Wave, Modifiers, *Profile, Plan, Error));
	TestNotNull(TEXT("Right upper-arm FK becomes a mirrored left-arm source"), FindTrack(Plan, TEXT("mirror_left_upperarm.pitch")));
	TestNotNull(TEXT("Right lower-arm FK becomes a mirrored left-arm source"), FindTrack(Plan, TEXT("mirror_left_lowerarm.pitch")));
	TestNotNull(TEXT("Right hand FK becomes a mirrored left-hand source"), FindTrack(Plan, TEXT("mirror_left_hand.pitch")));
	TestNotNull(TEXT("Right finger pose becomes left finger pose"), FindTrack(Plan, TEXT("left_fingers.open")));
	TestNull(TEXT("Mirrored plan contains no right upper-arm FK"), FindTrack(Plan, TEXT("right_upperarm.pitch")));
	TestNull(TEXT("Mirrored plan contains no hand-anchor IK"), FindTrack(Plan, TEXT("left_hand.ik")));

	const FLLMMotionTrack* RightPitch = FindTrack(RightPlan, TEXT("right_upperarm.pitch"));
	const FLLMMotionTrack* LeftPitch = FindTrack(Plan, TEXT("mirror_left_upperarm.pitch"));
	const FLLMMotionTrack* RightYaw = FindTrack(RightPlan, TEXT("right_upperarm.yaw"));
	const FLLMMotionTrack* LeftYaw = FindTrack(Plan, TEXT("mirror_left_upperarm.yaw"));
	const FLLMMotionTrack* RightRoll = FindTrack(RightPlan, TEXT("right_upperarm.roll"));
	const FLLMMotionTrack* LeftRoll = FindTrack(Plan, TEXT("mirror_left_upperarm.roll"));
	if (RightPitch && LeftPitch && RightYaw && LeftYaw && RightRoll && LeftRoll)
	{
		TestEqual(TEXT("Mirror compiler preserves source Pitch for basis-aware execution"), LeftPitch->FloatKeys[3].V, RightPitch->FloatKeys[3].V);
		TestEqual(TEXT("Mirror compiler preserves source Yaw for basis-aware execution"), LeftYaw->FloatKeys[3].V, RightYaw->FloatKeys[3].V);
		TestEqual(TEXT("Mirror compiler preserves source Roll for basis-aware execution"), LeftRoll->FloatKeys[3].V, RightRoll->FloatKeys[3].V);
	}

	FLLMProceduralPoseSnapshot Snapshot;
	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	FLLMNPCMotionSampler::SampleClip(Plan.Clip, nullptr, nullptr, EmptyTargets, 0.67f, Snapshot);
	TestFalse(TEXT("Mirrored sampling drives left-arm FK"), Snapshot.LeftUpperArmAdditiveRotation.IsNearlyZero());
	TestTrue(TEXT("Mirrored sampling marks FK values as right-side source data"), Snapshot.bLeftArmFKMirroredSource);
	TestTrue(TEXT("Mirrored sampling leaves right-arm FK inactive"), Snapshot.RightUpperArmAdditiveRotation.IsNearlyZero());
	TestEqual(TEXT("Mirrored sampling leaves both IK solvers inactive"), Snapshot.LeftHandIKAlpha + Snapshot.RightHandIKAlpha, 0.0f);

	FLLMProceduralPoseSnapshot RightSnapshot;
	FLLMNPCMotionSampler::SampleClip(RightPlan.Clip, nullptr, nullptr, EmptyTargets, 0.67f, RightSnapshot);
	if (const USkeleton* Skeleton = Profile->Skeleton.LoadSynchronous())
	{
		const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
		const int32 RightUpperIndex = ReferenceSkeleton.FindBoneIndex(TEXT("upperarm_r"));
		const int32 RightLowerIndex = ReferenceSkeleton.FindBoneIndex(TEXT("lowerarm_r"));
		const int32 RightHandIndex = ReferenceSkeleton.FindBoneIndex(TEXT("hand_r"));
		const int32 LeftUpperIndex = ReferenceSkeleton.FindBoneIndex(TEXT("upperarm_l"));
		const int32 LeftLowerIndex = ReferenceSkeleton.FindBoneIndex(TEXT("lowerarm_l"));
		const int32 LeftHandIndex = ReferenceSkeleton.FindBoneIndex(TEXT("hand_l"));
		const int32 RightParentIndex = ReferenceSkeleton.GetParentIndex(RightUpperIndex);
		const int32 LeftParentIndex = ReferenceSkeleton.GetParentIndex(LeftUpperIndex);
		const FVector RightHand = EvaluateReferenceHandPosition(
			*Skeleton,
			false,
			RightSnapshot.RightUpperArmAdditiveRotation,
			RightSnapshot.RightLowerArmAdditiveRotation,
			RightSnapshot.RightHandAdditiveRotation
		);
		const FTransform RightParent = BuildReferenceComponentTransform(ReferenceSkeleton, RightParentIndex);
		const FTransform LeftParent = BuildReferenceComponentTransform(ReferenceSkeleton, LeftParentIndex);
		const FLLMNPCArmChainTransforms RightOriginal = {
				BuildReferenceComponentTransform(ReferenceSkeleton, RightUpperIndex),
				BuildReferenceComponentTransform(ReferenceSkeleton, RightLowerIndex),
				BuildReferenceComponentTransform(ReferenceSkeleton, RightHandIndex)
		};
		const FLLMNPCArmChainTransforms LeftOriginal = {
			BuildReferenceComponentTransform(ReferenceSkeleton, LeftUpperIndex),
			BuildReferenceComponentTransform(ReferenceSkeleton, LeftLowerIndex),
			BuildReferenceComponentTransform(ReferenceSkeleton, LeftHandIndex)
		};
		const FLLMNPCArmChainTransforms MirroredChain = FLLMNPCMotionMirror::MirrorRightArmFKAcrossSkeletonX(
			RightParent,
			RightOriginal,
			Snapshot.LeftUpperArmAdditiveRotation,
			Snapshot.LeftLowerArmAdditiveRotation,
			Snapshot.LeftHandAdditiveRotation,
			LeftParent,
			LeftOriginal,
			1.0f
		);
		const FLLMNPCArmChainTransforms ZeroAlphaChain = FLLMNPCMotionMirror::MirrorRightArmFKAcrossSkeletonX(
			RightParent,
			RightOriginal,
			Snapshot.LeftUpperArmAdditiveRotation,
			Snapshot.LeftLowerArmAdditiveRotation,
			Snapshot.LeftHandAdditiveRotation,
			LeftParent,
			LeftOriginal,
			0.0f
		);
		TestTrue(TEXT("Zero mirror alpha preserves the current left upper arm"), ZeroAlphaChain.UpperCS.Equals(LeftOriginal.UpperCS, 0.001f));
		TestTrue(TEXT("Zero mirror alpha preserves the current left lower arm"), ZeroAlphaChain.LowerCS.Equals(LeftOriginal.LowerCS, 0.001f));
		TestTrue(TEXT("Zero mirror alpha preserves the current left hand"), ZeroAlphaChain.HandCS.Equals(LeftOriginal.HandCS, 0.001f));
		const FVector LeftHand = MirroredChain.HandCS.GetLocation();
		AddInfo(FString::Printf(
			TEXT("Basis mirror right=%s left=%s target=%s"),
			*RightHand.ToCompactString(),
			*LeftHand.ToCompactString(),
			*FVector(-RightHand.X, RightHand.Y, RightHand.Z).ToCompactString()
		));
		TestTrue(TEXT("Mirrored FK reflects Manny's skeletal lateral axis"), FMath::IsNearlyEqual(LeftHand.X, -RightHand.X, 1.0f));
		TestTrue(TEXT("Mirrored FK preserves skeletal forward placement"), FMath::IsNearlyEqual(LeftHand.Y, RightHand.Y, 1.0f));
		TestTrue(TEXT("Mirrored FK preserves hand height"), FMath::IsNearlyEqual(LeftHand.Z, RightHand.Z, 1.0f));
	}
	else
	{
		AddError(TEXT("Manny Skeleton did not load for mirror geometry validation."));
	}
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
