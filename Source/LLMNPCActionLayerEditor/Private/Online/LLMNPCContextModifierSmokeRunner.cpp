#include "Online/LLMNPCContextModifierSmokeRunner.h"

#include "Async/Async.h"
#include "Context/LLMNPCContextModifierResolver.h"
#include "Context/LLMNPCModifierMappingProfile.h"
#include "Dialogue/LLMNPCConversationSession.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Dialogue/LLMNPCModelTurnValidator.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "LLMNPCSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Online/LLMNPCOnlineReportSanitizer.h"
#include "Online/LLMNPCOnlineTestConfigLoader.h"
#include "Providers/LLMNPCDeepSeekProvider.h"
#include "Providers/LLMNPCProviderCredentials.h"
#include "Selection/LLMNPCCandidateRetriever.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCTemplateCompiler.h"
#include "Templates/LLMNPCTemplateLibrarySubsystem.h"
#include "UObject/StrongObjectPtr.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMNPCContextModifierSmoke, Log, All);

namespace
{
constexpr int32 ContextSmokeRunsPerCase = 3;
constexpr float ContextSmokeTolerance = 0.001f;

enum class ELLMNPCContextSmokeAdaptation : uint8
{
	NearTarget,
	FarTarget,
	HighTarget,
	SelectedHandOccupied,
	ExcitedEmotion,
	Walking
};

struct FLLMNPCContextSmokeCase
{
	FName CaseId = NAME_None;
	FString UserMessage;
	FName ExpectedSelectionId = NAME_None;
	FString ExpectedTargetRef;
	ELLMNPCContextSmokeAdaptation Adaptation =
		ELLMNPCContextSmokeAdaptation::NearTarget;
	FName ExpectedTraceStage = NAME_None;
	FString ContextVisibility = TEXT("execution_context_only");
	FLLMNPCSelectionContextSnapshot BaselineSelection;
	FLLMNPCSelectionContextSnapshot ContextSelection;
	FLLMNPCExecutionContextSnapshot BaselineExecution;
	FLLMNPCExecutionContextSnapshot ContextExecution;
};

struct FLLMNPCContextSmokeSample
{
	FName CaseId = NAME_None;
	int32 Iteration = 0;
	FGuid RequestId;
	FString RequestHash;
	FString ContextVisibility;
	TArray<FName> OfferedSelectionIds;
	FName ExpectedSelectionId = NAME_None;
	FName ObservedSelectionId = NAME_None;
	FName ResolvedTemplateId = NAME_None;
	FString ExpectedTargetRef;
	FString ObservedTargetRef;
	FLLMNPCSelectedAction ModelSuggestion;
	FLLMNPCTemplateModifiers ValidatedRequest;
	FLLMNPCResolvedMotionModifiers BaselineResolved;
	FLLMNPCResolvedMotionModifiers ContextResolved;
	FLLMNPCModifierResolutionTrace BaselineTrace;
	FLLMNPCModifierResolutionTrace ContextTrace;
	FLLMNPCExecutionContextSnapshot EffectiveContextExecution;
	bool bProviderSuccess = false;
	bool bProviderMatches = false;
	bool bModelMatches = false;
	bool bConfigMatches = false;
	bool bPrivacyScanPassed = false;
	bool bSchemaValid = false;
	bool bSelectionPolicyValid = false;
	bool bValidatorAccepted = false;
	bool bExpectedSelection = false;
	bool bBaselineResolved = false;
	bool bContextResolved = false;
	bool bPolicyBounded = false;
	bool bTraceComplete = false;
	bool bFinalValuesDiffer = false;
	bool bAdaptationPassed = false;
	bool bCompiled = false;
	bool bPassed = false;
	FName ErrorCode = NAME_None;
	int32 HttpStatus = 0;
	int32 AttemptCount = 0;
	float LatencySeconds = -1.0f;
	int32 PromptTokens = INDEX_NONE;
	int32 CompletionTokens = INDEX_NONE;
	int32 TotalTokens = INDEX_NONE;
};

struct FLLMNPCActiveContextModifierSmoke
{
	bool bExitEditorWhenComplete = false;
	int32 CaseIndex = 0;
	int32 Iteration = 0;
	FGuid SessionId = FGuid::NewGuid();
	FGuid CurrentRequestId;
	FString CurrentRequestHash;
	bool bCurrentPrivacyScanPassed = false;
	FLLMNPCOnlineTestConfigState Config;
	TStrongObjectPtr<UGameInstance> GameInstance;
	TStrongObjectPtr<ULLMNPCTemplateLibrarySubsystem> Library;
	TStrongObjectPtr<ULLMNPCSkeletonProfile> Profile;
	TStrongObjectPtr<ULLMNPCModifierMappingProfile> MappingProfile;
	TSharedPtr<FLLMNPCDeepSeekProvider> Provider;
	TArray<FLLMNPCTemplateCandidate> BaseCandidates;
	TArray<FLLMNPCTemplateCandidate> CurrentOfferedCandidates;
	TArray<FLLMNPCContextSmokeCase> Cases;
	TArray<FLLMNPCContextSmokeSample> Samples;
};

TSharedPtr<FLLMNPCActiveContextModifierSmoke> ActiveContextModifierSmoke;

FLLMNPCSelectionContextSnapshot ContextSmokeNeutralSelection(
	bool bIncludePlayerTarget
)
{
	FLLMNPCSelectionContextSnapshot Result;
	Result.Personality.ProfileId = TEXT("manny_n3_neutral");
	Result.Personality.Expressiveness = 1.0f;
	Result.Personality.Shyness = 0.0f;
	Result.Personality.Sociability = 0.5f;
	Result.Emotion.PrimaryEmotion = TEXT("neutral");
	Result.Relationship.OtherActorRef = TEXT("player");
	if (bIncludePlayerTarget)
	{
		FLLMNPCSceneTargetContext& Target =
			Result.AvailableTargets.AddDefaulted_GetRef();
		Target.TargetRef = TEXT("player");
		Target.Category = TEXT("scene_target");
		Target.SemanticTags = { TEXT("person"), TEXT("player") };
		Target.Salience = 1.0f;
	}
	return Result;
}

FLLMNPCExecutionContextSnapshot ContextSmokeBaselineExecution(
	bool bIncludePlayerTarget
)
{
	FLLMNPCExecutionContextSnapshot Result;
	Result.AvailableSpace = 1.0f;
	Result.RightObstacle.Clearance = 1.0f;
	Result.LeftObstacle.Clearance = 1.0f;
	if (bIncludePlayerTarget)
	{
		Result.Target.TargetRef = TEXT("player");
		Result.Target.bValid = true;
		Result.Target.LocationCS = FVector(240.0f, 0.0f, 0.0f);
		Result.Target.DirectionCS = FVector::ForwardVector;
		Result.Target.DistanceCm = 240.0f;
	}
	return Result;
}

TArray<FLLMNPCContextSmokeCase> ContextSmokeBuildCases()
{
	TArray<FLLMNPCContextSmokeCase> Cases;
	auto AddPointCase = [&Cases](
		const TCHAR* CaseId,
		ELLMNPCContextSmokeAdaptation Adaptation
	) -> FLLMNPCContextSmokeCase&
	{
		FLLMNPCContextSmokeCase& TestCase = Cases.AddDefaulted_GetRef();
		TestCase.CaseId = CaseId;
		TestCase.UserMessage =
			TEXT("Show me where the player is by pointing at them.");
		TestCase.ExpectedSelectionId = TEXT("gesture.point.target");
		TestCase.ExpectedTargetRef = TEXT("player");
		TestCase.Adaptation = Adaptation;
		TestCase.ExpectedTraceStage = TEXT("target_geometry");
		TestCase.BaselineSelection = ContextSmokeNeutralSelection(true);
		TestCase.ContextSelection = TestCase.BaselineSelection;
		TestCase.BaselineExecution = ContextSmokeBaselineExecution(true);
		TestCase.ContextExecution = TestCase.BaselineExecution;
		return TestCase;
	};
	auto AddWaveCase = [&Cases](
		const TCHAR* CaseId,
		ELLMNPCContextSmokeAdaptation Adaptation,
		const TCHAR* TraceStage
	) -> FLLMNPCContextSmokeCase&
	{
		FLLMNPCContextSmokeCase& TestCase = Cases.AddDefaulted_GetRef();
		TestCase.CaseId = CaseId;
		TestCase.UserMessage = TEXT("Greet me with a friendly wave.");
		TestCase.ExpectedSelectionId = TEXT("gesture.wave.right");
		TestCase.Adaptation = Adaptation;
		TestCase.ExpectedTraceStage = TraceStage;
		TestCase.BaselineSelection = ContextSmokeNeutralSelection(false);
		TestCase.ContextSelection = TestCase.BaselineSelection;
		TestCase.BaselineExecution = ContextSmokeBaselineExecution(false);
		TestCase.ContextExecution = TestCase.BaselineExecution;
		return TestCase;
	};

	FLLMNPCContextSmokeCase& Near = AddPointCase(
		TEXT("point_near_target"),
		ELLMNPCContextSmokeAdaptation::NearTarget
	);
	Near.ContextExecution.Target.LocationCS = FVector(75.0f, 0.0f, 0.0f);
	Near.ContextExecution.Target.DistanceCm = 75.0f;

	FLLMNPCContextSmokeCase& Far = AddPointCase(
		TEXT("point_far_target"),
		ELLMNPCContextSmokeAdaptation::FarTarget
	);
	Far.ContextExecution.Target.LocationCS = FVector(450.0f, 0.0f, 0.0f);
	Far.ContextExecution.Target.DistanceCm = 450.0f;

	FLLMNPCContextSmokeCase& High = AddPointCase(
		TEXT("point_high_target"),
		ELLMNPCContextSmokeAdaptation::HighTarget
	);
	High.ContextExecution.Target.LocationCS = FVector(240.0f, 0.0f, 160.0f);
	High.ContextExecution.Target.DirectionCS =
		High.ContextExecution.Target.LocationCS.GetSafeNormal();
	High.ContextExecution.Target.DistanceCm =
		High.ContextExecution.Target.LocationCS.Size();
	High.ContextExecution.Target.HeightRelativeCm = 160.0f;

	AddWaveCase(
		TEXT("wave_selected_hand_occupied"),
		ELLMNPCContextSmokeAdaptation::SelectedHandOccupied,
		TEXT("movement_state")
	);

	FLLMNPCContextSmokeCase& Excited = AddWaveCase(
		TEXT("wave_excited"),
		ELLMNPCContextSmokeAdaptation::ExcitedEmotion,
		TEXT("emotion")
	);
	Excited.ContextVisibility = TEXT("model_visible_semantic_context");
	Excited.ContextSelection.Emotion.PrimaryEmotion = TEXT("excited");
	Excited.ContextSelection.Emotion.Intensity = 0.9f;
	Excited.ContextSelection.Emotion.Valence = 0.8f;
	Excited.ContextSelection.Emotion.Arousal = 0.9f;

	FLLMNPCContextSmokeCase& Walking = AddWaveCase(
		TEXT("wave_while_walking"),
		ELLMNPCContextSmokeAdaptation::Walking,
		TEXT("movement_state")
	);
	Walking.ContextExecution.MovementMode =
		ELLMNPCExecutionMovementMode::Walking;
	Walking.ContextExecution.OwnerSpeedCmPerSecond = 180.0f;
	Walking.ContextExecution.OwnerVelocityCS = FVector(180.0f, 0.0f, 0.0f);
	return Cases;
}

TArray<TSharedPtr<FJsonValue>> ContextSmokeNamesToJson(
	const TArray<FName>& Names
)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

bool ContextSmokePayloadIsPrivate(
	const FString& Payload,
	FString& OutForbiddenToken
)
{
	OutForbiddenToken.Reset();
	for (const TCHAR* Forbidden : {
		TEXT("\"bone\""),
		TEXT("\"bone_name\""),
		TEXT("\"transform\""),
		TEXT("\"quaternion\""),
		TEXT("\"compact_pose"),
		TEXT("\"component_space\""),
		TEXT("clavicle_"),
		TEXT("upperarm_"),
		TEXT("lowerarm_"),
		TEXT("hand_r"),
		TEXT("/Game/"),
		TEXT("/LLMNPCActionLayer/")
	})
	{
		if (Payload.Contains(Forbidden, ESearchCase::IgnoreCase))
		{
			OutForbiddenToken = Forbidden;
			return false;
		}
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(Payload);
	FString SchemaVersion;
	return
		FJsonSerializer::Deserialize(Reader, Root) &&
		Root.IsValid() &&
		Root->TryGetStringField(TEXT("schema_version"), SchemaVersion) &&
		SchemaVersion == TEXT("llmnpc.turn_request.v3");
}

bool ContextSmokeTraceHasStage(
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

bool ContextSmokeInRange(float Value, const FVector2D& Range)
{
	return
		FMath::IsFinite(Value) &&
		Value >= Range.X - ContextSmokeTolerance &&
		Value <= Range.Y + ContextSmokeTolerance;
}

bool ContextSmokePolicyBoundsPass(
	const ULLMNPCMotionTemplate& MotionTemplate,
	const FLLMNPCResolvedMotionModifiers& Modifiers
)
{
	const FLLMNPCModifierPolicy& Policy = MotionTemplate.ModifierPolicy;
	return
		!Modifiers.bNeedsFallbackSelection &&
		ContextSmokeInRange(Modifiers.Amplitude, Policy.AmplitudeRange) &&
		ContextSmokeInRange(Modifiers.SpeedScale, Policy.SpeedRange) &&
		ContextSmokeInRange(Modifiers.DurationScale, Policy.DurationRange) &&
		ContextSmokeInRange(Modifiers.ReachScale, Policy.ReachScaleRange) &&
		ContextSmokeInRange(Modifiers.HeightScale, Policy.HeightScaleRange) &&
		ContextSmokeInRange(Modifiers.LateralScale, Policy.LateralScaleRange) &&
		ContextSmokeInRange(
			Modifiers.GazeEngagement,
			Policy.GazeEngagementRange
		) &&
		ContextSmokeInRange(
			Modifiers.PalmOrientationWeight,
			Policy.PalmOrientationWeightRange
		) &&
		ContextSmokeInRange(
			Modifiers.FingerPoseWeight,
			Policy.FingerPoseWeightRange
		) &&
		ContextSmokeInRange(
			Modifiers.TorsoParticipation,
			Policy.TorsoParticipationRange
		) &&
		ContextSmokeInRange(Modifiers.BlendInScale, Policy.BlendInScaleRange) &&
		ContextSmokeInRange(
			Modifiers.BlendOutScale,
			Policy.BlendOutScaleRange
		) &&
		(!Modifiers.bMirror || Policy.bAllowMirror);
}

bool ContextSmokeResolvedValuesDiffer(
	const FLLMNPCResolvedMotionModifiers& A,
	const FLLMNPCResolvedMotionModifiers& B
)
{
	return
		!FMath::IsNearlyEqual(A.Amplitude, B.Amplitude) ||
		!FMath::IsNearlyEqual(A.SpeedScale, B.SpeedScale) ||
		!FMath::IsNearlyEqual(A.DurationScale, B.DurationScale) ||
		!FMath::IsNearlyEqual(A.ReachScale, B.ReachScale) ||
		!FMath::IsNearlyEqual(A.HeightScale, B.HeightScale) ||
		!FMath::IsNearlyEqual(A.LateralScale, B.LateralScale) ||
		!FMath::IsNearlyEqual(A.GazeEngagement, B.GazeEngagement) ||
		!FMath::IsNearlyEqual(
			A.PalmOrientationWeight,
			B.PalmOrientationWeight
		) ||
		!FMath::IsNearlyEqual(A.FingerPoseWeight, B.FingerPoseWeight) ||
		!FMath::IsNearlyEqual(A.TorsoParticipation, B.TorsoParticipation) ||
		A.bMirror != B.bMirror ||
		A.bNeedsFallbackSelection != B.bNeedsFallbackSelection;
}

bool ContextSmokeExpectedAdaptationPasses(
	ELLMNPCContextSmokeAdaptation Adaptation,
	const FLLMNPCResolvedMotionModifiers& Baseline,
	const FLLMNPCResolvedMotionModifiers& Context,
	const FLLMNPCModifierResolutionTrace& Trace
)
{
	switch (Adaptation)
	{
	case ELLMNPCContextSmokeAdaptation::NearTarget:
		return
			Context.ReachScale <
				Baseline.ReachScale - ContextSmokeTolerance &&
			ContextSmokeTraceHasStage(Trace, TEXT("target_geometry"));
	case ELLMNPCContextSmokeAdaptation::FarTarget:
		return
			Context.ReachScale >
				Baseline.ReachScale + ContextSmokeTolerance &&
			ContextSmokeTraceHasStage(Trace, TEXT("target_geometry"));
	case ELLMNPCContextSmokeAdaptation::HighTarget:
		return
			Context.HeightScale <
				Baseline.HeightScale - ContextSmokeTolerance &&
			ContextSmokeTraceHasStage(Trace, TEXT("target_geometry"));
	case ELLMNPCContextSmokeAdaptation::SelectedHandOccupied:
		return
			Context.bMirror != Baseline.bMirror &&
			ContextSmokeTraceHasStage(Trace, TEXT("movement_state"));
	case ELLMNPCContextSmokeAdaptation::ExcitedEmotion:
		return ContextSmokeTraceHasStage(Trace, TEXT("emotion"));
	case ELLMNPCContextSmokeAdaptation::Walking:
		return
			(
				Context.Amplitude <
					Baseline.Amplitude - ContextSmokeTolerance ||
				Context.ReachScale <
					Baseline.ReachScale - ContextSmokeTolerance ||
				Context.TorsoParticipation <
					Baseline.TorsoParticipation - ContextSmokeTolerance
			) &&
			ContextSmokeTraceHasStage(Trace, TEXT("movement_state"));
	default:
		return false;
	}
}

TSharedRef<FJsonObject> ContextSmokeRequestedModifiersToJson(
	const FLLMNPCTemplateModifiers& Modifiers
)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("target_ref"), Modifiers.TargetRef);
	Object->SetNumberField(TEXT("amplitude"), Modifiers.Amplitude);
	Object->SetNumberField(TEXT("speed_scale"), Modifiers.SpeedScale);
	Object->SetNumberField(TEXT("duration_scale"), Modifiers.DurationScale);
	Object->SetStringField(TEXT("style"), Modifiers.Style.ToString());
	Object->SetBoolField(TEXT("mirror"), Modifiers.bMirror);
	Object->SetNumberField(TEXT("random_seed"), Modifiers.RandomSeed);
	return Object;
}

TSharedRef<FJsonObject> ContextSmokeModelSuggestionToJson(
	const FLLMNPCSelectedAction& Action
)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("decision"), Action.Decision.ToString());
	Object->SetStringField(TEXT("selection_id"), Action.TemplateId.ToString());
	Object->SetStringField(TEXT("target_ref"), Action.TargetRef);
	Object->SetNumberField(TEXT("amplitude"), Action.Amplitude);
	Object->SetNumberField(TEXT("speed_scale"), Action.SpeedScale);
	Object->SetNumberField(TEXT("duration_scale"), Action.DurationScale);
	Object->SetStringField(TEXT("style"), Action.Style.ToString());
	Object->SetStringField(TEXT("reason_tag"), Action.ReasonTag.ToString());
	Object->SetBoolField(TEXT("mirror"), Action.bMirror);
	Object->SetNumberField(TEXT("random_seed"), Action.RandomSeed);
	return Object;
}

TSharedRef<FJsonObject> ContextSmokeResolvedToJson(
	const FLLMNPCResolvedMotionModifiers& Modifiers
)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetNumberField(TEXT("amplitude"), Modifiers.Amplitude);
	Object->SetNumberField(TEXT("speed_scale"), Modifiers.SpeedScale);
	Object->SetNumberField(TEXT("duration_scale"), Modifiers.DurationScale);
	Object->SetNumberField(TEXT("reach_scale"), Modifiers.ReachScale);
	Object->SetNumberField(TEXT("height_scale"), Modifiers.HeightScale);
	Object->SetNumberField(TEXT("lateral_scale"), Modifiers.LateralScale);
	Object->SetNumberField(TEXT("cycle_count"), Modifiers.CycleCount);
	Object->SetNumberField(
		TEXT("gaze_engagement"),
		Modifiers.GazeEngagement
	);
	Object->SetNumberField(
		TEXT("palm_orientation_weight"),
		Modifiers.PalmOrientationWeight
	);
	Object->SetNumberField(
		TEXT("finger_pose_weight"),
		Modifiers.FingerPoseWeight
	);
	Object->SetNumberField(
		TEXT("torso_participation"),
		Modifiers.TorsoParticipation
	);
	Object->SetNumberField(TEXT("blend_in_scale"), Modifiers.BlendInScale);
	Object->SetNumberField(TEXT("blend_out_scale"), Modifiers.BlendOutScale);
	Object->SetBoolField(TEXT("mirror"), Modifiers.bMirror);
	Object->SetStringField(TEXT("target_ref"), Modifiers.TargetRef);
	Object->SetStringField(TEXT("style"), Modifiers.Style.ToString());
	Object->SetNumberField(TEXT("random_seed"), Modifiers.RandomSeed);
	Object->SetBoolField(
		TEXT("needs_fallback_selection"),
		Modifiers.bNeedsFallbackSelection
	);
	Object->SetStringField(
		TEXT("result_code"),
		Modifiers.ResultCode.ToString()
	);
	return Object;
}

TArray<TSharedPtr<FJsonValue>> ContextSmokeTraceToJson(
	const FLLMNPCModifierResolutionTrace& Trace
)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FLLMNPCModifierResolutionStep& Step : Trace.Steps)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("stage"), Step.Stage.ToString());
		Object->SetStringField(TEXT("modifier"), Step.Modifier.ToString());
		Object->SetStringField(TEXT("operation"), Step.Operation.ToString());
		Object->SetNumberField(TEXT("before"), Step.Before);
		Object->SetNumberField(TEXT("contribution"), Step.Contribution);
		Object->SetNumberField(TEXT("after"), Step.After);
		Object->SetStringField(TEXT("reason"), Step.Reason);
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	return Values;
}

TSharedRef<FJsonObject> ContextSmokeExecutionToJson(
	const FLLMNPCExecutionContextSnapshot& Context
)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(
		TEXT("movement_mode"),
		StaticEnum<ELLMNPCExecutionMovementMode>()->GetNameStringByValue(
			static_cast<int64>(Context.MovementMode)
		)
	);
	Object->SetNumberField(
		TEXT("owner_speed_cm_per_second"),
		Context.OwnerSpeedCmPerSecond
	);
	Object->SetBoolField(
		TEXT("right_hand_occupied"),
		Context.bRightHandOccupied
	);
	Object->SetBoolField(
		TEXT("left_hand_occupied"),
		Context.bLeftHandOccupied
	);
	Object->SetBoolField(
		TEXT("upper_body_occupied"),
		Context.bUpperBodyOccupied
	);
	Object->SetNumberField(TEXT("available_space"), Context.AvailableSpace);
	TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
	Target->SetStringField(TEXT("target_ref"), Context.Target.TargetRef);
	Target->SetBoolField(TEXT("valid"), Context.Target.bValid);
	Target->SetNumberField(TEXT("distance_cm"), Context.Target.DistanceCm);
	Target->SetNumberField(
		TEXT("height_relative_cm"),
		Context.Target.HeightRelativeCm
	);
	Object->SetObjectField(TEXT("target"), Target);
	return Object;
}

FName ContextSmokeResolveError(
	const FLLMNPCContextSmokeSample& Sample,
	const FLLMNPCModelTurnResult& Result,
	const FString& ParserError,
	const FString& PolicyError,
	const FString& ValidatorError,
	const FString& BaselineError,
	const FString& ContextError,
	const FString& CompilerError
)
{
	if (!Result.bSuccess)
	{
		return Result.ErrorCode.IsNone()
			? FName(TEXT("LLMNPC_N3_PROVIDER_FAILED"))
			: Result.ErrorCode;
	}
	if (!Sample.bProviderMatches)
	{
		return TEXT("LLMNPC_N3_PROVIDER_MISMATCH");
	}
	if (!Sample.bModelMatches)
	{
		return TEXT("LLMNPC_N3_MODEL_MISMATCH");
	}
	if (!Sample.bConfigMatches)
	{
		return TEXT("LLMNPC_N3_CONFIG_CHANGED");
	}
	if (!Sample.bPrivacyScanPassed)
	{
		return TEXT("LLMNPC_N3_REQUEST_PRIVACY_FAILED");
	}
	if (!Sample.bSchemaValid)
	{
		return ParserError.IsEmpty()
			? FName(TEXT("LLMNPC_N3_RESPONSE_SCHEMA_INVALID"))
			: FName(*ParserError);
	}
	if (!Sample.bSelectionPolicyValid)
	{
		return PolicyError.IsEmpty()
			? FName(TEXT("LLMNPC_N3_SELECTION_POLICY_REJECTED"))
			: FName(*PolicyError);
	}
	if (!Sample.bValidatorAccepted)
	{
		return ValidatorError.IsEmpty()
			? FName(TEXT("LLMNPC_N3_VALIDATOR_REJECTED"))
			: FName(*ValidatorError);
	}
	if (!Sample.bExpectedSelection)
	{
		return TEXT("LLMNPC_N3_SELECTION_EXPECTATION_MISMATCH");
	}
	if (!Sample.bBaselineResolved)
	{
		return BaselineError.IsEmpty()
			? FName(TEXT("LLMNPC_N3_BASELINE_RESOLVE_FAILED"))
			: FName(*BaselineError);
	}
	if (!Sample.bContextResolved)
	{
		return ContextError.IsEmpty()
			? FName(TEXT("LLMNPC_N3_CONTEXT_RESOLVE_FAILED"))
			: FName(*ContextError);
	}
	if (!Sample.bPolicyBounded)
	{
		return TEXT("LLMNPC_N3_POLICY_BOUNDS_FAILED");
	}
	if (!Sample.bTraceComplete)
	{
		return TEXT("LLMNPC_N3_TRACE_INCOMPLETE");
	}
	if (!Sample.bAdaptationPassed)
	{
		return TEXT("LLMNPC_N3_EXPECTED_ADAPTATION_MISSING");
	}
	if (!Sample.bCompiled)
	{
		return CompilerError.IsEmpty()
			? FName(TEXT("LLMNPC_N3_TEMPLATE_COMPILE_FAILED"))
			: FName(*CompilerError);
	}
	return NAME_None;
}

bool ContextSmokeSaveReport(
	const FLLMNPCActiveContextModifierSmoke& Run,
	bool bPassed,
	FString& OutFilename,
	FString& OutError
)
{
	OutFilename.Reset();
	OutError.Reset();
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("schema_version"),
		TEXT("llmnpc.forward_n3_context_modifier_report.v1")
	);
	Root->SetStringField(
		TEXT("suite_version"),
		TEXT("forward_n3.context_modifier.v1")
	);
	Root->SetStringField(TEXT("status"), bPassed ? TEXT("passed") : TEXT("failed"));
	Root->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
	Root->SetNumberField(TEXT("runs_per_case"), ContextSmokeRunsPerCase);
	Root->SetNumberField(TEXT("case_count"), Run.Cases.Num());
	Root->SetNumberField(TEXT("sample_count"), Run.Samples.Num());
	Root->SetStringField(TEXT("catalog_hash"), Run.Library->GetCatalogHash());
	Root->SetStringField(TEXT("profile_id"), Run.Profile->ProfileId.ToString());
	Root->SetStringField(
		TEXT("profile_version"),
		Run.Profile->SemanticVersion
	);
	Root->SetStringField(
		TEXT("mapping_profile_id"),
		Run.MappingProfile->ProfileId.ToString()
	);
	Root->SetStringField(
		TEXT("mapping_schema_version"),
		Run.MappingProfile->SchemaVersion
	);
	Root->SetBoolField(TEXT("raw_requests_persisted"), false);
	Root->SetBoolField(TEXT("raw_responses_persisted"), false);

	TSharedRef<FJsonObject> Session = MakeShared<FJsonObject>();
	Session->SetStringField(
		TEXT("expected_provider"),
		TEXT("deepseek_direct_editor")
	);
	Session->SetStringField(TEXT("expected_model"), Run.Config.Model);
	Session->SetStringField(TEXT("endpoint_origin"), Run.Config.EndpointOrigin);
	Session->SetStringField(
		TEXT("non_secret_config_hash"),
		Run.Config.NonSecretConfigHash
	);
	Root->SetObjectField(TEXT("online_test_session"), Session);

	int32 PassedCount = 0;
	int32 PolicyCount = 0;
	int32 TraceCount = 0;
	int32 AdaptationCount = 0;
	int32 FinalDifferenceCount = 0;
	int64 TotalTokenSum = 0;
	int32 TokenSampleCount = 0;
	double LatencySum = 0.0;
	int32 LatencySampleCount = 0;
	TArray<TSharedPtr<FJsonValue>> Samples;
	for (const FLLMNPCContextSmokeSample& Sample : Run.Samples)
	{
		PassedCount += Sample.bPassed ? 1 : 0;
		PolicyCount += Sample.bPolicyBounded ? 1 : 0;
		TraceCount += Sample.bTraceComplete ? 1 : 0;
		AdaptationCount += Sample.bAdaptationPassed ? 1 : 0;
		FinalDifferenceCount += Sample.bFinalValuesDiffer ? 1 : 0;
		if (Sample.TotalTokens >= 0)
		{
			TotalTokenSum += Sample.TotalTokens;
			++TokenSampleCount;
		}
		if (Sample.LatencySeconds >= 0.0f)
		{
			LatencySum += Sample.LatencySeconds;
			++LatencySampleCount;
		}

		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("case_id"), Sample.CaseId.ToString());
		Object->SetNumberField(TEXT("iteration"), Sample.Iteration);
		Object->SetStringField(
			TEXT("request_id"),
			Sample.RequestId.ToString(EGuidFormats::DigitsWithHyphensLower)
		);
		Object->SetStringField(TEXT("request_hash"), Sample.RequestHash);
		Object->SetStringField(
			TEXT("context_visibility"),
			Sample.ContextVisibility
		);
		Object->SetArrayField(
			TEXT("offered_selection_ids"),
			ContextSmokeNamesToJson(Sample.OfferedSelectionIds)
		);
		Object->SetStringField(
			TEXT("expected_selection_id"),
			Sample.ExpectedSelectionId.ToString()
		);
		Object->SetStringField(
			TEXT("observed_selection_id"),
			Sample.ObservedSelectionId.ToString()
		);
		Object->SetStringField(
			TEXT("resolved_template_id"),
			Sample.ResolvedTemplateId.ToString()
		);
		Object->SetStringField(
			TEXT("expected_target_ref"),
			Sample.ExpectedTargetRef
		);
		Object->SetStringField(
			TEXT("observed_target_ref"),
			Sample.ObservedTargetRef
		);
		Object->SetObjectField(
			TEXT("model_suggestion"),
			ContextSmokeModelSuggestionToJson(Sample.ModelSuggestion)
		);
		Object->SetObjectField(
			TEXT("validated_model_request"),
			ContextSmokeRequestedModifiersToJson(Sample.ValidatedRequest)
		);
		Object->SetObjectField(
			TEXT("baseline_resolved"),
			ContextSmokeResolvedToJson(Sample.BaselineResolved)
		);
		Object->SetObjectField(
			TEXT("context_resolved"),
			ContextSmokeResolvedToJson(Sample.ContextResolved)
		);
		Object->SetObjectField(
			TEXT("execution_context"),
			ContextSmokeExecutionToJson(Sample.EffectiveContextExecution)
		);
		Object->SetArrayField(
			TEXT("baseline_resolution_trace"),
			ContextSmokeTraceToJson(Sample.BaselineTrace)
		);
		Object->SetArrayField(
			TEXT("context_resolution_trace"),
			ContextSmokeTraceToJson(Sample.ContextTrace)
		);
		Object->SetBoolField(
			TEXT("privacy_scan_passed"),
			Sample.bPrivacyScanPassed
		);
		Object->SetBoolField(TEXT("schema_valid"), Sample.bSchemaValid);
		Object->SetBoolField(
			TEXT("selection_policy_valid"),
			Sample.bSelectionPolicyValid
		);
		Object->SetBoolField(
			TEXT("validator_accepted"),
			Sample.bValidatorAccepted
		);
		Object->SetBoolField(
			TEXT("expected_selection"),
			Sample.bExpectedSelection
		);
		Object->SetBoolField(
			TEXT("policy_bounded"),
			Sample.bPolicyBounded
		);
		Object->SetBoolField(
			TEXT("trace_complete"),
			Sample.bTraceComplete
		);
		Object->SetBoolField(
			TEXT("final_values_differ"),
			Sample.bFinalValuesDiffer
		);
		Object->SetBoolField(
			TEXT("adaptation_passed"),
			Sample.bAdaptationPassed
		);
		Object->SetBoolField(TEXT("compiled"), Sample.bCompiled);
		Object->SetBoolField(TEXT("passed"), Sample.bPassed);
		Object->SetStringField(TEXT("error_code"), Sample.ErrorCode.ToString());
		Object->SetNumberField(TEXT("http_status"), Sample.HttpStatus);
		Object->SetNumberField(TEXT("attempt_count"), Sample.AttemptCount);
		Object->SetNumberField(TEXT("latency_seconds"), Sample.LatencySeconds);
		Object->SetNumberField(TEXT("prompt_tokens"), Sample.PromptTokens);
		Object->SetNumberField(
			TEXT("completion_tokens"),
			Sample.CompletionTokens
		);
		Object->SetNumberField(TEXT("total_tokens"), Sample.TotalTokens);
		Samples.Add(MakeShared<FJsonValueObject>(Object));
	}
	Root->SetArrayField(TEXT("samples"), Samples);

	const int32 SampleCount = Run.Samples.Num();
	const auto Rate = [SampleCount](int32 Count)
	{
		return SampleCount > 0
			? static_cast<double>(Count) / SampleCount
			: 0.0;
	};
	TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
	Metrics->SetNumberField(TEXT("pass_rate"), Rate(PassedCount));
	Metrics->SetNumberField(TEXT("policy_bound_rate"), Rate(PolicyCount));
	Metrics->SetNumberField(TEXT("trace_complete_rate"), Rate(TraceCount));
	Metrics->SetNumberField(TEXT("adaptation_pass_rate"), Rate(AdaptationCount));
	Metrics->SetNumberField(
		TEXT("final_difference_rate"),
		Rate(FinalDifferenceCount)
	);
	Metrics->SetNumberField(
		TEXT("average_latency_seconds"),
		LatencySampleCount > 0 ? LatencySum / LatencySampleCount : -1.0
	);
	Metrics->SetNumberField(
		TEXT("average_total_tokens"),
		TokenSampleCount > 0
			? static_cast<double>(TotalTokenSum) / TokenSampleCount
			: -1.0
	);
	Root->SetObjectField(TEXT("metrics"), Metrics);

	FString ReportJson;
	if (!FLLMNPCOnlineReportSanitizer::SanitizeAndSerialize(Root, ReportJson))
	{
		OutError = TEXT("LLMNPC_N3_REPORT_SERIALIZE_FAILED");
		return false;
	}
	FString ApiKey;
	ELLMNPCCredentialSource CredentialSource =
		ELLMNPCCredentialSource::Missing;
	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const bool bHasCredential =
		Settings &&
		FLLMNPCProviderCredentials::ResolveDeepSeekApiKey(
			*Settings,
			ApiKey,
			CredentialSource
		);
	static_cast<void>(CredentialSource);
	const bool bSecretDetected =
		(bHasCredential && !ApiKey.IsEmpty() && ReportJson.Contains(ApiKey)) ||
		ReportJson.Contains(TEXT("Bearer "), ESearchCase::IgnoreCase) ||
		ReportJson.Contains(
			TEXT("OPENAI_API_KEY"),
			ESearchCase::IgnoreCase
		);
	ApiKey.Reset();
	if (bSecretDetected)
	{
		OutError = TEXT("LLMNPC_N3_REPORT_SECRET_DETECTED");
		return false;
	}

	const FString Directory = FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("LLMNPCActionLayer/ForwardN3/Reports")
	);
	if (!IFileManager::Get().MakeDirectory(*Directory, true))
	{
		OutError = TEXT("LLMNPC_N3_REPORT_DIRECTORY_FAILED");
		return false;
	}
	OutFilename = FPaths::Combine(
		Directory,
		FString::Printf(
			TEXT("context_modifier_%s.json"),
			*FDateTime::UtcNow().ToString(TEXT("%Y%m%d_%H%M%S"))
		)
	);
	if (!FFileHelper::SaveStringToFile(
		ReportJson,
		*OutFilename,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	))
	{
		OutFilename.Reset();
		OutError = TEXT("LLMNPC_N3_REPORT_WRITE_FAILED");
		return false;
	}
	return true;
}

void ContextSmokeFinish(
	const TSharedPtr<FLLMNPCActiveContextModifierSmoke>& Run
)
{
	if (!Run.IsValid() || ActiveContextModifierSmoke != Run)
	{
		return;
	}
	const int32 ExpectedSampleCount =
		Run->Cases.Num() * ContextSmokeRunsPerCase;
	const bool bPassed =
		Run->Samples.Num() == ExpectedSampleCount &&
		!Run->Samples.ContainsByPredicate(
			[](const FLLMNPCContextSmokeSample& Sample)
			{
				return !Sample.bPassed;
			}
		);
	FString ReportFilename;
	FString ReportError;
	const bool bSaved = ContextSmokeSaveReport(
		*Run,
		bPassed,
		ReportFilename,
		ReportError
	);
	const FLLMNPCContextSmokeSample* LastSample =
		Run->Samples.IsEmpty() ? nullptr : &Run->Samples.Last();
	FLLMNPCOnlineTestConfigLoader::RecordConnectionTest(
		bPassed && bSaved,
		FLLMNPCProviderCredentials::DeepSeekProviderId(),
		Run->Config.Model,
		Run->Config.NonSecretConfigHash,
		bPassed && bSaved
			? NAME_None
			: FName(TEXT("LLMNPC_N3_CONTEXT_SUITE_FAILED")),
		LastSample ? LastSample->HttpStatus : 0,
		LastSample ? LastSample->LatencySeconds : -1.0f
	);
	if (bPassed && bSaved)
	{
		UE_LOG(
			LogLLMNPCContextModifierSmoke,
			Display,
			TEXT("LLMNPC Forward N3 Context Modifier PASSED. Samples=%d Catalog=%s Model=%s Report=%s"),
			Run->Samples.Num(),
			*Run->Library->GetCatalogHash(),
			*Run->Config.Model,
			*ReportFilename
		);
	}
	else
	{
		UE_LOG(
			LogLLMNPCContextModifierSmoke,
			Error,
			TEXT("LLMNPC Forward N3 Context Modifier FAILED. Samples=%d Expected=%d ReportError=%s Report=%s"),
			Run->Samples.Num(),
			ExpectedSampleCount,
			*ReportError,
			*ReportFilename
		);
	}
	const bool bExit = Run->bExitEditorWhenComplete;
	ActiveContextModifierSmoke.Reset();
	if (bExit)
	{
		FPlatformMisc::RequestExit(false);
	}
}

bool ContextSmokePrepareRequest(
	const TSharedPtr<FLLMNPCActiveContextModifierSmoke>& Run,
	FLLMNPCModelTurnRequest& OutRequest,
	FString& OutError
)
{
	OutError.Reset();
	const FLLMNPCContextSmokeCase& TestCase = Run->Cases[Run->CaseIndex];
	FLLMNPCCandidateRetrievalRequest RetrievalRequest;
	RetrievalRequest.UserMessage = TestCase.UserMessage;
	RetrievalRequest.SourceCandidates = Run->BaseCandidates;
	RetrievalRequest.Context = TestCase.ContextSelection;
	RetrievalRequest.MaxCandidates = 8;
	RetrievalRequest.RepeatSuppressionSeconds = 0.0f;
	const FLLMNPCCandidateRetrievalResult Retrieval =
		ULLMNPCCandidateRetriever::Retrieve(RetrievalRequest);
	Run->CurrentOfferedCandidates = Retrieval.Candidates;
	if (!Run->CurrentOfferedCandidates.ContainsByPredicate(
		[&TestCase](const FLLMNPCTemplateCandidate& Candidate)
		{
			return Candidate.SelectionId == TestCase.ExpectedSelectionId;
		}
	))
	{
		OutError = TEXT("LLMNPC_N3_EXPECTED_CANDIDATE_NOT_OFFERED");
		return false;
	}

	ULLMNPCConversationSession* Session =
		NewObject<ULLMNPCConversationSession>();
	Session->InitializeSession(TEXT("manny_n3_context_eval"), 8);
	Session->AddMessage(ELLMNPCDialogueRole::Player, TestCase.UserMessage);
	Run->CurrentRequestId = FGuid::NewGuid();
	const FString ContextJson = Session->BuildContextualRequestJsonForSchema(
		Run->CurrentRequestId,
		Run->CurrentOfferedCandidates,
		TestCase.ContextSelection,
		TEXT("llmnpc.selection_prompt.v3"),
		TEXT("llmnpc.turn_request.v3")
	);
	FString ForbiddenToken;
	Run->bCurrentPrivacyScanPassed =
		ContextSmokePayloadIsPrivate(ContextJson, ForbiddenToken);
	if (!Run->bCurrentPrivacyScanPassed)
	{
		OutError = ForbiddenToken.IsEmpty()
			? TEXT("LLMNPC_N3_REQUEST_SCHEMA_OR_PRIVACY_INVALID")
			: FString::Printf(
				TEXT("LLMNPC_N3_REQUEST_FORBIDDEN_TOKEN:%s"),
				*ForbiddenToken
			);
		return false;
	}
	Run->CurrentRequestHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*ContextJson)
	);
	OutRequest.RequestId = Run->CurrentRequestId;
	OutRequest.SessionId = Run->SessionId;
	OutRequest.NPCId = TEXT("manny_n3_context_eval");
	OutRequest.UserMessage = TestCase.UserMessage;
	OutRequest.ContextJson = ContextJson;
	return true;
}

void ContextSmokeStartNext(
	const TSharedPtr<FLLMNPCActiveContextModifierSmoke>& Run
);

void ContextSmokeCompleteSample(
	const TSharedPtr<FLLMNPCActiveContextModifierSmoke>& Run,
	const FLLMNPCModelTurnResult& Result
)
{
	if (!Run.IsValid() || ActiveContextModifierSmoke != Run)
	{
		return;
	}
	const FLLMNPCContextSmokeCase& TestCase = Run->Cases[Run->CaseIndex];
	FLLMNPCContextSmokeSample Sample;
	Sample.CaseId = TestCase.CaseId;
	Sample.Iteration = Run->Iteration + 1;
	Sample.RequestId = Run->CurrentRequestId;
	Sample.RequestHash = Run->CurrentRequestHash;
	Sample.ContextVisibility = TestCase.ContextVisibility;
	Sample.ExpectedSelectionId = TestCase.ExpectedSelectionId;
	Sample.ExpectedTargetRef = TestCase.ExpectedTargetRef;
	Sample.bProviderSuccess = Result.bSuccess;
	Sample.bProviderMatches =
		Result.ProviderId == FLLMNPCProviderCredentials::DeepSeekProviderId();
	Sample.bModelMatches =
		!Run->Config.Model.IsEmpty() &&
		Result.ProviderModelId == Run->Config.Model;
	const FLLMNPCOnlineTestConfigState CurrentConfig =
		FLLMNPCOnlineTestConfigLoader::GetState();
	Sample.bConfigMatches =
		CurrentConfig.IsLoaded() &&
		CurrentConfig.NonSecretConfigHash == Run->Config.NonSecretConfigHash;
	Sample.bPrivacyScanPassed = Run->bCurrentPrivacyScanPassed;
	Sample.HttpStatus = Result.HttpStatus;
	Sample.AttemptCount = Result.AttemptCount;
	Sample.LatencySeconds = Result.TotalLatencySeconds;
	Sample.PromptTokens = Result.PromptTokens;
	Sample.CompletionTokens = Result.CompletionTokens;
	Sample.TotalTokens = Result.TotalTokens;
	for (const FLLMNPCTemplateCandidate& Candidate : Run->CurrentOfferedCandidates)
	{
		Sample.OfferedSelectionIds.Add(Candidate.SelectionId);
	}

	FString ParserError;
	FString PolicyError;
	FString ValidatorError;
	FString BaselineError;
	FString ContextError;
	FString CompilerError;
	FLLMNPCModelTurnDecision Decision;
	const ULLMNPCMotionTemplate* ResolvedTemplate = nullptr;
	if (Result.bSuccess)
	{
		Sample.bSchemaValid = FLLMNPCModelTurnParser::Parse(
			Result.ResponseJson,
			Decision,
			ParserError
		);
	}
	if (Sample.bSchemaValid)
	{
		Sample.ModelSuggestion = Decision.Action;
		Sample.ObservedSelectionId = Decision.Action.TemplateId;
		Sample.ObservedTargetRef = Decision.Action.TargetRef;
		Sample.bSelectionPolicyValid =
			ULLMNPCCandidateRetriever::ApplySelectionPolicy(
				Decision,
				Run->CurrentOfferedCandidates,
				PolicyError
			);
	}
	if (Sample.bSelectionPolicyValid)
	{
		Sample.bValidatorAccepted =
			FLLMNPCModelTurnValidator::ValidateAndResolve(
				Decision,
				*Run->Library,
				Run->Profile->ProfileId,
				ResolvedTemplate,
				Sample.ValidatedRequest,
				ValidatorError
			);
	}
	Sample.bExpectedSelection =
		Sample.bValidatorAccepted &&
		Decision.Action.Decision == TEXT("execute_template") &&
		Decision.Locomotion.Decision == TEXT("none") &&
		Sample.ObservedSelectionId == TestCase.ExpectedSelectionId &&
		(TestCase.ExpectedTargetRef.IsEmpty() ||
			Sample.ObservedTargetRef == TestCase.ExpectedTargetRef);
	if (Sample.bValidatorAccepted && ResolvedTemplate)
	{
		Sample.ResolvedTemplateId = ResolvedTemplate->Metadata.TemplateId;
		Sample.bBaselineResolved = FLLMNPCContextModifierResolver::Resolve(
			*ResolvedTemplate,
			Sample.ValidatedRequest,
			TestCase.BaselineSelection,
			TestCase.BaselineExecution,
			Run->MappingProfile.Get(),
			Run->Profile.Get(),
			Sample.BaselineResolved,
			Sample.BaselineTrace,
			BaselineError
		);
		if (Sample.bBaselineResolved)
		{
			Sample.EffectiveContextExecution = TestCase.ContextExecution;
			if (
				TestCase.Adaptation ==
				ELLMNPCContextSmokeAdaptation::SelectedHandOccupied
			)
			{
				Sample.EffectiveContextExecution.bRightHandOccupied =
					!Sample.BaselineResolved.bMirror;
				Sample.EffectiveContextExecution.bLeftHandOccupied =
					Sample.BaselineResolved.bMirror;
			}
			Sample.bContextResolved =
				FLLMNPCContextModifierResolver::Resolve(
					*ResolvedTemplate,
					Sample.ValidatedRequest,
					TestCase.ContextSelection,
					Sample.EffectiveContextExecution,
					Run->MappingProfile.Get(),
					Run->Profile.Get(),
					Sample.ContextResolved,
					Sample.ContextTrace,
					ContextError
				);
		}
		if (Sample.bBaselineResolved && Sample.bContextResolved)
		{
			Sample.bPolicyBounded =
				ContextSmokePolicyBoundsPass(
					*ResolvedTemplate,
					Sample.BaselineResolved
				) &&
				ContextSmokePolicyBoundsPass(
					*ResolvedTemplate,
					Sample.ContextResolved
				);
			Sample.bTraceComplete =
				ContextSmokeTraceHasStage(
					Sample.BaselineTrace,
					TEXT("model_request")
				) &&
				ContextSmokeTraceHasStage(
					Sample.ContextTrace,
					TEXT("model_request")
				) &&
				ContextSmokeTraceHasStage(
					Sample.ContextTrace,
					TestCase.ExpectedTraceStage
				);
			Sample.bFinalValuesDiffer = ContextSmokeResolvedValuesDiffer(
				Sample.BaselineResolved,
				Sample.ContextResolved
			);
			Sample.bAdaptationPassed =
				ContextSmokeExpectedAdaptationPasses(
					TestCase.Adaptation,
					Sample.BaselineResolved,
					Sample.ContextResolved,
					Sample.ContextTrace
				);
			if (!Sample.ContextResolved.bNeedsFallbackSelection)
			{
				FLLMMotionPlan Plan;
				Sample.bCompiled = FLLMNPCTemplateCompiler::Compile(
					*ResolvedTemplate,
					Sample.ContextResolved.ToLegacyModifiers(),
					*Run->Profile,
					Plan,
					CompilerError
				);
				if (Sample.bCompiled)
				{
					FLLMNPCContextModifierResolver::ApplyToCompiledPlan(
						*ResolvedTemplate,
						Sample.ContextResolved,
						Plan
					);
				}
			}
		}
	}
	Sample.ErrorCode = ContextSmokeResolveError(
		Sample,
		Result,
		ParserError,
		PolicyError,
		ValidatorError,
		BaselineError,
		ContextError,
		CompilerError
	);
	Sample.bPassed =
		Sample.bProviderSuccess &&
		Sample.bProviderMatches &&
		Sample.bModelMatches &&
		Sample.bConfigMatches &&
		Sample.bPrivacyScanPassed &&
		Sample.bSchemaValid &&
		Sample.bSelectionPolicyValid &&
		Sample.bValidatorAccepted &&
		Sample.bExpectedSelection &&
		Sample.bBaselineResolved &&
		Sample.bContextResolved &&
		Sample.bPolicyBounded &&
		Sample.bTraceComplete &&
		Sample.bAdaptationPassed &&
		Sample.bCompiled;
	Run->Samples.Add(Sample);

	if (Sample.bPassed)
	{
		UE_LOG(
			LogLLMNPCContextModifierSmoke,
			Display,
			TEXT("N3 context sample %s %d/%d: PASS observed=%s resolved=%s delta=%s latency=%.3fs"),
			*TestCase.CaseId.ToString(),
			Run->Iteration + 1,
			ContextSmokeRunsPerCase,
			*Sample.ObservedSelectionId.ToString(),
			*Sample.ResolvedTemplateId.ToString(),
			Sample.bFinalValuesDiffer ? TEXT("yes") : TEXT("no"),
			Sample.LatencySeconds
		);
	}
	else
	{
		UE_LOG(
			LogLLMNPCContextModifierSmoke,
			Error,
			TEXT("N3 context sample %s %d/%d: FAIL observed=%s resolved=%s delta=%s error=%s latency=%.3fs"),
			*TestCase.CaseId.ToString(),
			Run->Iteration + 1,
			ContextSmokeRunsPerCase,
			*Sample.ObservedSelectionId.ToString(),
			*Sample.ResolvedTemplateId.ToString(),
			Sample.bFinalValuesDiffer ? TEXT("yes") : TEXT("no"),
			*Sample.ErrorCode.ToString(),
			Sample.LatencySeconds
		);
	}

	++Run->Iteration;
	if (Run->Iteration >= ContextSmokeRunsPerCase)
	{
		Run->Iteration = 0;
		++Run->CaseIndex;
	}
	ContextSmokeStartNext(Run);
}

void ContextSmokeStartNext(
	const TSharedPtr<FLLMNPCActiveContextModifierSmoke>& Run
)
{
	if (!Run.IsValid() || ActiveContextModifierSmoke != Run)
	{
		return;
	}
	if (Run->CaseIndex >= Run->Cases.Num())
	{
		ContextSmokeFinish(Run);
		return;
	}
	FLLMNPCModelTurnRequest Request;
	FString Error;
	if (!ContextSmokePrepareRequest(Run, Request, Error))
	{
		FLLMNPCContextSmokeSample Sample;
		Sample.CaseId = Run->Cases[Run->CaseIndex].CaseId;
		Sample.Iteration = Run->Iteration + 1;
		Sample.ErrorCode = FName(*Error);
		Run->Samples.Add(Sample);
		UE_LOG(
			LogLLMNPCContextModifierSmoke,
			Error,
			TEXT("N3 context request preparation failed: %s"),
			*Error
		);
		Run->CaseIndex = Run->Cases.Num();
		ContextSmokeFinish(Run);
		return;
	}

	const TWeakPtr<FLLMNPCActiveContextModifierSmoke> WeakRun = Run;
	Run->Provider->SendTurn(
		Request,
		[WeakRun](const FLLMNPCModelTurnResult& Result)
		{
			AsyncTask(
				ENamedThreads::GameThread,
				[WeakRun, Result]()
				{
					if (
						const TSharedPtr<FLLMNPCActiveContextModifierSmoke>
							Pinned = WeakRun.Pin()
					)
					{
						ContextSmokeCompleteSample(Pinned, Result);
					}
				}
			);
		}
	);
}
}

bool FLLMNPCContextModifierSmokeRunner::Start(
	bool bExitEditorWhenComplete,
	FString& OutError
)
{
	OutError.Reset();
	if (ActiveContextModifierSmoke.IsValid())
	{
		OutError = TEXT("LLMNPC_N3_CONTEXT_SMOKE_ALREADY_RUNNING");
		return false;
	}
	const FLLMNPCOnlineTestConfigState Config =
		FLLMNPCOnlineTestConfigLoader::LoadProjectConfig();
	if (!Config.IsLoaded() || !Config.bCredentialPresent)
	{
		OutError = Config.ErrorCode.IsNone()
			? TEXT("LLMNPC_N3_CONTEXT_CONFIG_NOT_READY")
			: Config.ErrorCode.ToString();
		return false;
	}

	TSharedPtr<FLLMNPCActiveContextModifierSmoke> Run =
		MakeShared<FLLMNPCActiveContextModifierSmoke>();
	Run->bExitEditorWhenComplete = bExitEditorWhenComplete;
	Run->Config = Config;
	Run->GameInstance =
		TStrongObjectPtr<UGameInstance>(NewObject<UGameInstance>());
	Run->Library = TStrongObjectPtr<ULLMNPCTemplateLibrarySubsystem>(
		NewObject<ULLMNPCTemplateLibrarySubsystem>(Run->GameInstance.Get())
	);
	Run->Library->RefreshLibrary();
	if (
		!Run->Library->GetScanErrors().IsEmpty() ||
		Run->Library->GetPublishedPublicActionCount() < 3
	)
	{
		OutError = TEXT("LLMNPC_N3_CONTEXT_CATALOG_NOT_READY");
		return false;
	}
	Run->Profile = TStrongObjectPtr<ULLMNPCSkeletonProfile>(
		LoadObject<ULLMNPCSkeletonProfile>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
		)
	);
	Run->MappingProfile = TStrongObjectPtr<ULLMNPCModifierMappingProfile>(
		LoadObject<ULLMNPCModifierMappingProfile>(
			nullptr,
			TEXT("/LLMNPCActionLayer/LLMNPC/Context/MP_Manny_Default_v1.MP_Manny_Default_v1")
		)
	);
	if (!Run->Profile.IsValid() || !Run->MappingProfile.IsValid())
	{
		OutError = TEXT("LLMNPC_N3_CONTEXT_MANNY_ASSETS_MISSING");
		return false;
	}
	FString MappingError;
	if (!Run->MappingProfile->Validate(MappingError))
	{
		OutError = MappingError;
		return false;
	}
	Run->Library->QueryRuntimeCandidates(
		Run->Profile->ProfileId,
		Run->BaseCandidates
	);
	if (Run->BaseCandidates.Num() < 3)
	{
		OutError = TEXT("LLMNPC_N3_CONTEXT_CANDIDATES_MISSING");
		return false;
	}

	Run->Cases = ContextSmokeBuildCases();
	Run->Provider = MakeShared<FLLMNPCDeepSeekProvider>();
	ActiveContextModifierSmoke = Run;
	UE_LOG(
		LogLLMNPCContextModifierSmoke,
		Display,
		TEXT("LLMNPC Forward N3 Context Modifier started. Cases=%d RunsPerCase=%d Model=%s Catalog=%s"),
		Run->Cases.Num(),
		ContextSmokeRunsPerCase,
		*Run->Config.Model,
		*Run->Library->GetCatalogHash()
	);
	ContextSmokeStartNext(Run);
	return true;
}

bool FLLMNPCContextModifierSmokeRunner::IsRunning()
{
	return ActiveContextModifierSmoke.IsValid();
}

void FLLMNPCContextModifierSmokeRunner::Cancel()
{
	const TSharedPtr<FLLMNPCActiveContextModifierSmoke> Run =
		ActiveContextModifierSmoke;
	ActiveContextModifierSmoke.Reset();
	if (
		Run.IsValid() &&
		Run->Provider.IsValid() &&
		Run->CurrentRequestId.IsValid()
	)
	{
		Run->Provider->CancelRequest(Run->CurrentRequestId);
	}
}
