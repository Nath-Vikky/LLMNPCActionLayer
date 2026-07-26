#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Context/LLMNPCContextModifierResolver.h"
#include "Context/LLMNPCModifierMappingProfile.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "JsonObjectConverter.h"
#include "LLMNPCMotionComponent.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"

namespace
{
constexpr uint32 ForwardN3TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

const ULLMNPCMotionTemplate* ForwardN3LoadTemplate(const TCHAR* AssetName)
{
	return LoadObject<ULLMNPCMotionTemplate>(
		nullptr,
		*FString::Printf(
			TEXT("/LLMNPCActionLayer/LLMNPC/MotionTemplates/Manny/%s.%s"),
			AssetName,
			AssetName
		)
	);
}

const ULLMNPCSkeletonProfile* ForwardN3LoadProfile()
{
	return LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
}

const ULLMNPCModifierMappingProfile* ForwardN3LoadMapping()
{
	return LoadObject<ULLMNPCModifierMappingProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/Context/MP_Manny_Default_v1.MP_Manny_Default_v1")
	);
}

bool ForwardN3Resolve(
	const ULLMNPCMotionTemplate& MotionTemplate,
	const FLLMNPCSelectionContextSnapshot& Selection,
	const FLLMNPCExecutionContextSnapshot& Execution,
	FLLMNPCResolvedMotionModifiers& OutModifiers,
	FLLMNPCModifierResolutionTrace& OutTrace,
	const FLLMNPCTemplateModifiers& Requested =
		FLLMNPCTemplateModifiers()
)
{
	FString Error;
	return FLLMNPCContextModifierResolver::Resolve(
		MotionTemplate,
		Requested,
		Selection,
		Execution,
		ForwardN3LoadMapping(),
		ForwardN3LoadProfile(),
		OutModifiers,
		OutTrace,
		Error
	);
}

bool ForwardN3TraceHasStage(
	const FLLMNPCModifierResolutionTrace& Trace,
	FName Stage
)
{
	return Trace.Steps.ContainsByPredicate(
		[Stage](const FLLMNPCModifierResolutionStep& Step)
		{
			return Step.Stage == Stage;
		}
	);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN3ContextMappingTest,
	"LLMNPCActionLayer.ForwardN3.Resolver.ContextMapping",
	ForwardN3TestFlags
)

bool FLLMNPCForwardN3ContextMappingTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCMotionTemplate* Wave =
		ForwardN3LoadTemplate(TEXT("MT_Wave_Right_Manny_FK_v1"));
	const ULLMNPCMotionTemplate* Point =
		ForwardN3LoadTemplate(TEXT("MT_Point_Target_Manny_v1"));
	const ULLMNPCModifierMappingProfile* Mapping =
		ForwardN3LoadMapping();
	TestNotNull(TEXT("The Published Manny FK Wave loads"), Wave);
	TestNotNull(TEXT("The Published Manny Point loads"), Point);
	TestNotNull(TEXT("The Manny N3 mapping profile loads"), Mapping);
	if (!Wave || !Point || !Mapping)
	{
		return false;
	}
	FString MappingError;
	TestTrue(
		TEXT("The Manny N3 mapping profile validates"),
		Mapping->Validate(MappingError)
	);

	FLLMNPCExecutionContextSnapshot Execution;
	FLLMNPCSelectionContextSnapshot Neutral;
	FLLMNPCResolvedMotionModifiers NeutralWave;
	FLLMNPCModifierResolutionTrace NeutralTrace;
	TestTrue(
		TEXT("Neutral Wave context resolves"),
		ForwardN3Resolve(
			*Wave,
			Neutral,
			Execution,
			NeutralWave,
			NeutralTrace
		)
	);
	TestTrue(
		TEXT("Default Manny personality preserves baseline amplitude"),
		FMath::IsNearlyEqual(NeutralWave.Amplitude, 1.0f, 0.001f)
	);

	FLLMNPCSelectionContextSnapshot Shy = Neutral;
	Shy.Personality.Shyness = 1.0f;
	Shy.Personality.PersonalityTags.Add(TEXT("shy"));
	FLLMNPCResolvedMotionModifiers ShyWave;
	FLLMNPCModifierResolutionTrace ShyTrace;
	TestTrue(
		TEXT("Shy Wave context resolves"),
		ForwardN3Resolve(*Wave, Shy, Execution, ShyWave, ShyTrace)
	);
	TestTrue(
		TEXT("Shyness reduces Wave amplitude"),
		ShyWave.Amplitude < NeutralWave.Amplitude
	);
	TestTrue(
		TEXT("Shyness is visible in the personality trace stage"),
		ForwardN3TraceHasStage(ShyTrace, TEXT("personality"))
	);

	FLLMNPCSelectionContextSnapshot Excited = Neutral;
	Excited.Emotion.PrimaryEmotion = TEXT("excited");
	Excited.Emotion.Intensity = 1.0f;
	FLLMNPCResolvedMotionModifiers ExcitedWave;
	FLLMNPCModifierResolutionTrace ExcitedTrace;
	TestTrue(
		TEXT("Excited Wave context resolves"),
		ForwardN3Resolve(
			*Wave,
			Excited,
			Execution,
			ExcitedWave,
			ExcitedTrace
		)
	);
	TestTrue(
		TEXT("Excitement increases Wave amplitude within policy"),
		ExcitedWave.Amplitude > NeutralWave.Amplitude
	);
	TestTrue(
		TEXT("Excitement is visible in the emotion trace stage"),
		ForwardN3TraceHasStage(ExcitedTrace, TEXT("emotion"))
	);

	FLLMNPCExecutionContextSnapshot PointExecution;
	PointExecution.Target.bValid = true;
	PointExecution.Target.TargetRef = TEXT("player");
	PointExecution.Target.DistanceCm = 200.0f;
	FLLMNPCTemplateModifiers PointRequest;
	PointRequest.TargetRef = TEXT("player");
	FLLMNPCSelectionContextSnapshot Distrust = Neutral;
	Distrust.Relationship.Trust = -1.0f;
	FLLMNPCSelectionContextSnapshot Trust = Neutral;
	Trust.Relationship.Trust = 1.0f;
	FLLMNPCResolvedMotionModifiers DistrustPoint;
	FLLMNPCResolvedMotionModifiers TrustPoint;
	FLLMNPCModifierResolutionTrace DistrustTrace;
	FLLMNPCModifierResolutionTrace TrustTrace;
	TestTrue(
		TEXT("Distrust Point context resolves"),
		ForwardN3Resolve(
			*Point,
			Distrust,
			PointExecution,
			DistrustPoint,
			DistrustTrace,
			PointRequest
		)
	);
	TestTrue(
		TEXT("Trust Point context resolves"),
		ForwardN3Resolve(
			*Point,
			Trust,
			PointExecution,
			TrustPoint,
			TrustTrace,
			PointRequest
		)
	);
	TestTrue(
		TEXT("High trust permits more direct gaze"),
		TrustPoint.GazeEngagement > DistrustPoint.GazeEngagement
	);
	TestTrue(
		TEXT("Relationship contribution is traced"),
		ForwardN3TraceHasStage(TrustTrace, TEXT("relationship"))
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN3GeometryOccupancyTest,
	"LLMNPCActionLayer.ForwardN3.Resolver.GeometryOccupancyAndPolicy",
	ForwardN3TestFlags
)

bool FLLMNPCForwardN3GeometryOccupancyTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCMotionTemplate* Point =
		ForwardN3LoadTemplate(TEXT("MT_Point_Target_Manny_v1"));
	const ULLMNPCMotionTemplate* Wave =
		ForwardN3LoadTemplate(TEXT("MT_Wave_Right_Manny_FK_v1"));
	TestNotNull(TEXT("Point template loads"), Point);
	TestNotNull(TEXT("Wave template loads"), Wave);
	if (!Point || !Wave)
	{
		return false;
	}

	FLLMNPCSelectionContextSnapshot Selection;
	FLLMNPCTemplateModifiers PointRequest;
	PointRequest.TargetRef = TEXT("player");
	FLLMNPCExecutionContextSnapshot Medium;
	Medium.Target.bValid = true;
	Medium.Target.TargetRef = TEXT("player");
	Medium.Target.DistanceCm = 220.0f;
	FLLMNPCExecutionContextSnapshot Near = Medium;
	Near.Target.DistanceCm = 80.0f;
	FLLMNPCExecutionContextSnapshot Far = Medium;
	Far.Target.DistanceCm = 500.0f;
	FLLMNPCExecutionContextSnapshot High = Medium;
	High.Target.HeightRelativeCm = 220.0f;

	FLLMNPCResolvedMotionModifiers NearModifiers;
	FLLMNPCResolvedMotionModifiers MediumModifiers;
	FLLMNPCResolvedMotionModifiers FarModifiers;
	FLLMNPCResolvedMotionModifiers HighModifiers;
	FLLMNPCModifierResolutionTrace Trace;
	TestTrue(
		TEXT("Near Point resolves"),
		ForwardN3Resolve(
			*Point,
			Selection,
			Near,
			NearModifiers,
			Trace,
			PointRequest
		)
	);
	TestTrue(
		TEXT("Medium Point resolves"),
		ForwardN3Resolve(
			*Point,
			Selection,
			Medium,
			MediumModifiers,
			Trace,
			PointRequest
		)
	);
	TestTrue(
		TEXT("Far Point resolves"),
		ForwardN3Resolve(
			*Point,
			Selection,
			Far,
			FarModifiers,
			Trace,
			PointRequest
		)
	);
	TestTrue(
		TEXT("High Point resolves"),
		ForwardN3Resolve(
			*Point,
			Selection,
			High,
			HighModifiers,
			Trace,
			PointRequest
		)
	);
	TestTrue(
		TEXT("Near targets reduce reach"),
		NearModifiers.ReachScale < MediumModifiers.ReachScale
	);
	TestTrue(
		TEXT("Far targets improve pointing clarity without exceeding policy"),
		FarModifiers.ReachScale > MediumModifiers.ReachScale &&
		FarModifiers.ReachScale <= Point->ModifierPolicy.ReachScaleRange.Y
	);
	TestTrue(
		TEXT("Unreachable target height is clamped"),
		HighModifiers.HeightScale < MediumModifiers.HeightScale
	);

	FLLMNPCExecutionContextSnapshot RightBusy;
	RightBusy.bRightHandOccupied = true;
	Selection.ActiveStates = { TEXT("right_hand_busy") };
	FLLMNPCResolvedMotionModifiers MirroredWave;
	FLLMNPCModifierResolutionTrace MirrorTrace;
	TestTrue(
		TEXT("Right-hand occupancy resolves"),
		ForwardN3Resolve(
			*Wave,
			Selection,
			RightBusy,
			MirroredWave,
			MirrorTrace
		)
	);
	TestTrue(
		TEXT("Right-hand occupancy mirrors a mirror-capable Wave"),
		MirroredWave.bMirror
	);

	FLLMNPCExecutionContextSnapshot BothBusy = RightBusy;
	BothBusy.bLeftHandOccupied = true;
	Selection.ActiveStates.Add(TEXT("left_hand_busy"));
	FLLMNPCResolvedMotionModifiers BusyFallback;
	FLLMNPCModifierResolutionTrace BusyTrace;
	TestTrue(
		TEXT("Double-hand occupancy returns a valid fallback decision"),
		ForwardN3Resolve(
			*Wave,
			Selection,
			BothBusy,
			BusyFallback,
			BusyTrace
		)
	);
	TestTrue(
		TEXT("Double-hand occupancy requests fallback selection"),
		BusyFallback.bNeedsFallbackSelection
	);
	TestEqual(
		TEXT("Double-hand occupancy has a stable result code"),
		BusyFallback.ResultCode,
		FName(TEXT("LLMNPC_MODIFIER_BOTH_HANDS_OCCUPIED"))
	);

	Selection.ActiveStates.Reset();
	FLLMNPCExecutionContextSnapshot Walking;
	Walking.MovementMode = ELLMNPCExecutionMovementMode::Walking;
	FLLMNPCResolvedMotionModifiers WalkingWave;
	FLLMNPCModifierResolutionTrace WalkingTrace;
	TestTrue(
		TEXT("Walking Wave resolves"),
		ForwardN3Resolve(
			*Wave,
			Selection,
			Walking,
			WalkingWave,
			WalkingTrace
		)
	);
	TestTrue(
		TEXT("Walking reduces Wave amplitude"),
		WalkingWave.Amplitude < 1.0f
	);

	FLLMNPCExecutionContextSnapshot Running = Walking;
	Running.MovementMode = ELLMNPCExecutionMovementMode::Running;
	FLLMNPCResolvedMotionModifiers RunningFallback;
	FLLMNPCModifierResolutionTrace RunningTrace;
	TestTrue(
		TEXT("Running Wave resolves to a local decision"),
		ForwardN3Resolve(
			*Wave,
			Selection,
			Running,
			RunningFallback,
			RunningTrace
		)
	);
	TestTrue(
		TEXT("Running forbids the fine hand gesture"),
		RunningFallback.bNeedsFallbackSelection
	);

	FLLMNPCExecutionContextSnapshot Obstacle;
	Obstacle.AvailableSpace = 0.45f;
	Obstacle.RightObstacle.bTested = true;
	Obstacle.RightObstacle.bBlockingHit = true;
	Obstacle.RightObstacle.Clearance = 0.45f;
	FLLMNPCResolvedMotionModifiers ObstacleWave;
	FLLMNPCModifierResolutionTrace ObstacleTrace;
	TestTrue(
		TEXT("Obstacle Wave resolves"),
		ForwardN3Resolve(
			*Wave,
			Selection,
			Obstacle,
			ObstacleWave,
			ObstacleTrace
		)
	);
	TestTrue(
		TEXT("Obstacle clearance reduces Wave amplitude"),
		ObstacleWave.Amplitude < 1.0f
	);
	TestTrue(
		TEXT("Obstacle adaptation is traced"),
		ForwardN3TraceHasStage(
			ObstacleTrace,
			TEXT("obstacle_adaptation")
		)
	);

	FLLMNPCTemplateModifiers ExtremeRequest;
	ExtremeRequest.Amplitude = 50.0f;
	ExtremeRequest.SpeedScale = 50.0f;
	ExtremeRequest.DurationScale = 50.0f;
	FLLMNPCResolvedMotionModifiers Clamped;
	FLLMNPCModifierResolutionTrace ClampTrace;
	TestTrue(
		TEXT("Extreme request resolves through policy clamp"),
		ForwardN3Resolve(
			*Wave,
			Selection,
			FLLMNPCExecutionContextSnapshot(),
			Clamped,
			ClampTrace,
			ExtremeRequest
		)
	);
	TestTrue(
		TEXT("Amplitude cannot exceed template policy"),
		Clamped.Amplitude <= Wave->ModifierPolicy.AmplitudeRange.Y
	);
	TestTrue(
		TEXT("Speed cannot exceed template policy"),
		Clamped.SpeedScale <= Wave->ModifierPolicy.SpeedRange.Y
	);
	TestTrue(
		TEXT("Duration cannot exceed template policy"),
		Clamped.DurationScale <= Wave->ModifierPolicy.DurationRange.Y
	);
	TestTrue(
		TEXT("Policy clamp is traced"),
		ForwardN3TraceHasStage(
			ClampTrace,
			TEXT("template_policy_clamp")
		)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN3DeterminismTest,
	"LLMNPCActionLayer.ForwardN3.Resolver.FixedSeedDeterminism",
	ForwardN3TestFlags
)

bool FLLMNPCForwardN3DeterminismTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	const ULLMNPCMotionTemplate* Wave =
		ForwardN3LoadTemplate(TEXT("MT_Wave_Right_Manny_FK_v1"));
	const ULLMNPCSkeletonProfile* Profile = ForwardN3LoadProfile();
	TestNotNull(TEXT("Wave template loads"), Wave);
	TestNotNull(TEXT("Manny profile loads"), Profile);
	if (!Wave || !Profile)
	{
		return false;
	}

	FLLMNPCTemplateModifiers Request;
	Request.Style = TEXT("excited");
	Request.RandomSeed = 1337;
	FLLMNPCSelectionContextSnapshot Selection;
	Selection.Emotion.PrimaryEmotion = TEXT("excited");
	Selection.Emotion.Intensity = 0.8f;
	FLLMNPCResolvedMotionModifiers ResolvedA;
	FLLMNPCResolvedMotionModifiers ResolvedB;
	FLLMNPCModifierResolutionTrace TraceA;
	FLLMNPCModifierResolutionTrace TraceB;
	TestTrue(
		TEXT("First deterministic resolve succeeds"),
		ForwardN3Resolve(
			*Wave,
			Selection,
			FLLMNPCExecutionContextSnapshot(),
			ResolvedA,
			TraceA,
			Request
		)
	);
	TestTrue(
		TEXT("Second deterministic resolve succeeds"),
		ForwardN3Resolve(
			*Wave,
			Selection,
			FLLMNPCExecutionContextSnapshot(),
			ResolvedB,
			TraceB,
			Request
		)
	);
	TestEqual(
		TEXT("Identical context produces an identical resolution trace"),
		TraceA.ToSummary(),
		TraceB.ToSummary()
	);

	FLLMMotionPlan PlanA;
	FLLMMotionPlan PlanB;
	FString Error;
	FLLMNPCTemplateResolvedModifiers LegacyA;
	FLLMNPCTemplateResolvedModifiers LegacyB;
	TestTrue(
		TEXT("First fixed-seed plan compiles"),
		FLLMNPCTemplateCompiler::Compile(
			*Wave,
			ResolvedA.ToLegacyModifiers(),
			*Profile,
			PlanA,
			Error,
			&LegacyA
		)
	);
	TestTrue(
		TEXT("Second fixed-seed plan compiles"),
		FLLMNPCTemplateCompiler::Compile(
			*Wave,
			ResolvedB.ToLegacyModifiers(),
			*Profile,
			PlanB,
			Error,
			&LegacyB
		)
	);
	FLLMNPCContextModifierResolver::ApplyToCompiledPlan(
		*Wave,
		ResolvedA,
		PlanA
	);
	FLLMNPCContextModifierResolver::ApplyToCompiledPlan(
		*Wave,
		ResolvedB,
		PlanB
	);
	FString JsonA;
	FString JsonB;
	FJsonObjectConverter::UStructToJsonObjectString(PlanA, JsonA);
	FJsonObjectConverter::UStructToJsonObjectString(PlanB, JsonB);
	TestEqual(
		TEXT("Fixed Seed and identical context produce byte-identical plans"),
		JsonA,
		JsonB
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN3DynamicTargetTest,
	"LLMNPCActionLayer.ForwardN3.Runtime.DynamicTargetSmoothing",
	ForwardN3TestFlags
)

bool FLLMNPCForwardN3DynamicTargetTest::RunTest(
	const FString& Parameters
)
{
	static_cast<void>(Parameters);
	AActor* Owner = NewObject<AActor>();
	AActor* Target = NewObject<AActor>();
	ULLMNPCMotionComponent* Motion =
		NewObject<ULLMNPCMotionComponent>(Owner);
	TestNotNull(TEXT("Transient motion component exists"), Motion);
	TestNotNull(TEXT("Transient target exists"), Target);
	if (!Motion || !Target)
	{
		return false;
	}
	USceneComponent* OwnerRoot = NewObject<USceneComponent>(Owner);
	USceneComponent* TargetRoot = NewObject<USceneComponent>(Target);
	Owner->SetRootComponent(OwnerRoot);
	Target->SetRootComponent(TargetRoot);
	Owner->SetActorLocation(FVector::ZeroVector);
	Target->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	TestEqual(
		TEXT("Transient target fixture can move"),
		Target->GetActorLocation(),
		FVector(100.0f, 0.0f, 0.0f)
	);
	Motion->RegisterTarget(TEXT("player"), Target);

	FLLMNPCActiveMotionPlan Active;
	Active.Request.ModifierPolicy.bEnableDynamicTargetTracking = true;
	Active.Request.ModifierPolicy.MaxTargetFollowSpeedCmPerSecond = 100.0f;
	Active.Request.ModifierPolicy.MaxTargetAngularSpeedDegreesPerSecond = 90.0f;
	Active.Request.ModifierPolicy.TargetInterpolationSpeed = 20.0f;
	Active.Request.ModifierPolicy.TargetTeleportThresholdCm = 250.0f;
	Active.Request.ModifierPolicy.TargetLostFadeSeconds = 0.2f;
	Active.Request.ModifierPolicy.TargetLossPolicy =
		ELLMNPCTargetLossPolicy::FadeOut;
	FLLMMotionTrack& Track =
		Active.Request.Plan.Clip.Tracks.AddDefaulted_GetRef();
	Track.ControlId = TEXT("right_hand.ik");
	Track.TrackType = ELLMMotionTrackType::IKReach;
	Track.TargetRef = TEXT("player");

	TestTrue(
		TEXT("Initial target sample is accepted"),
		Motion->UpdateActiveTargetSamples(Active, 0.1f)
	);
	const FLLMNPCTargetRuntimeSample* Initial =
		Active.Request.TargetSamples.Find(TEXT("player"));
	TestNotNull(TEXT("Initial target sample exists"), Initial);
	if (!Initial)
	{
		return false;
	}
	TestTrue(
		TEXT("Initial sample starts fully engaged"),
		FMath::IsNearlyEqual(Initial->Alpha, 1.0f)
	);

	const FVector FirstLocation = Initial->LocationWS;
	Target->SetActorLocation(FVector(200.0f, 0.0f, 0.0f));
	TestTrue(
		TEXT("Moving target remains valid"),
		Motion->UpdateActiveTargetSamples(Active, 0.1f)
	);
	const FLLMNPCTargetRuntimeSample* Moved =
		Active.Request.TargetSamples.Find(TEXT("player"));
	TestTrue(
		TEXT("Linear follow speed is bounded"),
		FVector::Dist(FirstLocation, Moved->LocationWS) <= 10.01f
	);

	const FVector BeforeTurn = Moved->LocationWS;
	Target->SetActorLocation(FVector(200.0f, 200.0f, 0.0f));
	TestTrue(
		TEXT("Turning target remains valid"),
		Motion->UpdateActiveTargetSamples(Active, 0.1f)
	);
	const FLLMNPCTargetRuntimeSample* Turned =
		Active.Request.TargetSamples.Find(TEXT("player"));
	TestTrue(
		TEXT("Angularly changing target still obeys linear speed"),
		FVector::Dist(BeforeTurn, Turned->LocationWS) <= 10.01f
	);
	const float TurnAngle = FMath::RadiansToDegrees(FMath::Acos(
		FMath::Clamp(
			FVector::DotProduct(
				BeforeTurn.GetSafeNormal(),
				Turned->LocationWS.GetSafeNormal()
			),
			-1.0f,
			1.0f
		)
	));
	TestTrue(
		TEXT("Target direction obeys the angular speed limit"),
		TurnAngle <= 9.01f
	);

	const FVector BeforeTeleport = Turned->LocationWS;
	const float AlphaBeforeTeleport = Turned->Alpha;
	Target->SetActorLocation(FVector(1000.0f, 1000.0f, 0.0f));
	TestTrue(
		TEXT("Teleport uses the configured fade policy"),
		Motion->UpdateActiveTargetSamples(Active, 0.05f)
	);
	const FLLMNPCTargetRuntimeSample* Teleported =
		Active.Request.TargetSamples.Find(TEXT("player"));
	TestTrue(
		TEXT("Teleport is detected"),
		Teleported->bTeleported
	);
	TestEqual(
		TEXT("Teleport does not snap the sampled target"),
		Teleported->LocationWS,
		BeforeTeleport
	);
	TestTrue(
		TEXT("Teleport begins fading target influence"),
		Teleported->Alpha < AlphaBeforeTeleport
	);
	return true;
}

#endif
