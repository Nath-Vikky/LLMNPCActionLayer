#include "Context/LLMNPCContextModifierResolver.h"

#include "Context/LLMNPCModifierMappingProfile.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "Style/LLMNPCStyleResolver.h"
#include "Templates/LLMNPCMotionTemplate.h"

namespace
{
const FName StageTemplateDefault(TEXT("template_default"));
const FName StageModelRequest(TEXT("model_request"));
const FName StageStylePreset(TEXT("style_preset"));
const FName StagePersonality(TEXT("personality"));
const FName StageEmotion(TEXT("emotion"));
const FName StageRelationship(TEXT("relationship"));
const FName StageMovement(TEXT("movement_state"));
const FName StageTarget(TEXT("target_geometry"));
const FName StageObstacle(TEXT("obstacle_adaptation"));
const FName StageTemplateClamp(TEXT("template_policy_clamp"));
const FName StageSkeletonClamp(TEXT("skeleton_capability_clamp"));

bool NameContains(const FName Name, const TCHAR* Token)
{
	return Name.ToString().Contains(Token, ESearchCase::IgnoreCase);
}

bool ArrayContainsToken(const TArray<FName>& Values, const TCHAR* Token)
{
	return Values.ContainsByPredicate(
		[Token](const FName Name)
		{
			return NameContains(Name, Token);
		}
	);
}

bool TrackContainsToken(
	const ULLMNPCMotionTemplate& Template,
	const TCHAR* Token
)
{
	return Template.ProceduralClip.Tracks.ContainsByPredicate(
		[Token](const FLLMMotionTrack& Track)
		{
			return NameContains(Track.ControlId, Token);
		}
	);
}

void AddStep(
	FLLMNPCModifierResolutionTrace& Trace,
	FName Stage,
	FName Modifier,
	FName Operation,
	float Before,
	float Contribution,
	float After,
	const FString& Reason
)
{
	FLLMNPCModifierResolutionStep& Step = Trace.Steps.AddDefaulted_GetRef();
	Step.Stage = Stage;
	Step.Modifier = Modifier;
	Step.Operation = Operation;
	Step.Before = Before;
	Step.Contribution = Contribution;
	Step.After = After;
	Step.Reason = Reason;
}

void Multiply(
	float& Value,
	float Contribution,
	FName Stage,
	FName Modifier,
	const FString& Reason,
	FLLMNPCModifierResolutionTrace& Trace
)
{
	const float Before = Value;
	Value *= Contribution;
	if (!FMath::IsNearlyEqual(Before, Value))
	{
		AddStep(
			Trace,
			Stage,
			Modifier,
			TEXT("multiply"),
			Before,
			Contribution,
			Value,
			Reason
		);
	}
}

void SetFallback(
	FName ResultCode,
	const FString& Reason,
	FLLMNPCResolvedMotionModifiers& Modifiers,
	FLLMNPCModifierResolutionTrace& Trace,
	FName Stage = StageMovement
)
{
	Modifiers.bNeedsFallbackSelection = true;
	Modifiers.ResultCode = ResultCode;
	Trace.bNeedsFallbackSelection = true;
	Trace.ResultCode = ResultCode;
	AddStep(
		Trace,
		Stage,
		TEXT("selection"),
		TEXT("fallback"),
		0.0f,
		0.0f,
		0.0f,
		Reason
	);
}

float ResolveRuleInput(
	const FLLMNPCModifierMappingRule& Rule,
	const FLLMNPCSelectionContextSnapshot& Context
)
{
	switch (Rule.InputField)
	{
	case ELLMNPCModifierInputField::PersonalityExpressiveness:
		return Context.Personality.Expressiveness;
	case ELLMNPCModifierInputField::PersonalityShyness:
		return Context.Personality.Shyness;
	case ELLMNPCModifierInputField::PersonalitySociability:
		return Context.Personality.Sociability;
	case ELLMNPCModifierInputField::EmotionIntensity:
		return Context.Emotion.Intensity;
	case ELLMNPCModifierInputField::EmotionValence:
		return Context.Emotion.Valence;
	case ELLMNPCModifierInputField::EmotionArousal:
		return Context.Emotion.Arousal;
	case ELLMNPCModifierInputField::RelationshipFamiliarity:
		return Context.Relationship.Familiarity;
	case ELLMNPCModifierInputField::RelationshipTrust:
		return Context.Relationship.Trust;
	case ELLMNPCModifierInputField::RelationshipAffinity:
		return Context.Relationship.Affinity;
	default:
		return 0.0f;
	}
}

bool RuleTagMatches(
	const FLLMNPCModifierMappingRule& Rule,
	const FLLMNPCSelectionContextSnapshot& Context
)
{
	if (Rule.RequiredTag.IsNone())
	{
		return true;
	}
	switch (Rule.Stage)
	{
	case ELLMNPCModifierMappingStage::Personality:
		return Context.Personality.PersonalityTags.Contains(Rule.RequiredTag);
	case ELLMNPCModifierMappingStage::Emotion:
		return
			Context.Emotion.PrimaryEmotion == Rule.RequiredTag;
	case ELLMNPCModifierMappingStage::Relationship:
		return Context.Relationship.RelationshipTags.Contains(Rule.RequiredTag);
	default:
		return false;
	}
}

float EvaluateCurve(float Alpha, ELLMNPCModifierResponseCurve Curve)
{
	const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
	switch (Curve)
	{
	case ELLMNPCModifierResponseCurve::SmoothStep:
		return T * T * (3.0f - 2.0f * T);
	case ELLMNPCModifierResponseCurve::EaseIn:
		return T * T;
	case ELLMNPCModifierResponseCurve::EaseOut:
		return 1.0f - FMath::Square(1.0f - T);
	default:
		return T;
	}
}

float* ResolveOutputField(
	FLLMNPCResolvedMotionModifiers& Modifiers,
	ELLMNPCResolvedModifierField Field
)
{
	switch (Field)
	{
	case ELLMNPCResolvedModifierField::Amplitude:
		return &Modifiers.Amplitude;
	case ELLMNPCResolvedModifierField::SpeedScale:
		return &Modifiers.SpeedScale;
	case ELLMNPCResolvedModifierField::DurationScale:
		return &Modifiers.DurationScale;
	case ELLMNPCResolvedModifierField::ReachScale:
		return &Modifiers.ReachScale;
	case ELLMNPCResolvedModifierField::HeightScale:
		return &Modifiers.HeightScale;
	case ELLMNPCResolvedModifierField::LateralScale:
		return &Modifiers.LateralScale;
	case ELLMNPCResolvedModifierField::GazeEngagement:
		return &Modifiers.GazeEngagement;
	case ELLMNPCResolvedModifierField::PalmOrientationWeight:
		return &Modifiers.PalmOrientationWeight;
	case ELLMNPCResolvedModifierField::FingerPoseWeight:
		return &Modifiers.FingerPoseWeight;
	case ELLMNPCResolvedModifierField::TorsoParticipation:
		return &Modifiers.TorsoParticipation;
	case ELLMNPCResolvedModifierField::BlendInScale:
		return &Modifiers.BlendInScale;
	case ELLMNPCResolvedModifierField::BlendOutScale:
		return &Modifiers.BlendOutScale;
	default:
		return nullptr;
	}
}

FName ResolveStageName(ELLMNPCModifierMappingStage Stage)
{
	switch (Stage)
	{
	case ELLMNPCModifierMappingStage::Personality:
		return StagePersonality;
	case ELLMNPCModifierMappingStage::Emotion:
		return StageEmotion;
	case ELLMNPCModifierMappingStage::Relationship:
		return StageRelationship;
	default:
		return NAME_None;
	}
}

void ApplyMappingRule(
	const FLLMNPCModifierMappingRule& Rule,
	const FLLMNPCSelectionContextSnapshot& Context,
	FLLMNPCResolvedMotionModifiers& Modifiers,
	FLLMNPCModifierResolutionTrace& Trace
)
{
	if (!RuleTagMatches(Rule, Context))
	{
		return;
	}
	float* Output = ResolveOutputField(Modifiers, Rule.OutputModifier);
	if (!Output)
	{
		return;
	}
	const float Input = ResolveRuleInput(Rule, Context);
	const float Denominator = Rule.InputRange.Y - Rule.InputRange.X;
	const float Alpha = Denominator > KINDA_SMALL_NUMBER
		? (Input - Rule.InputRange.X) / Denominator
		: 0.0f;
	const float Contribution = FMath::Lerp(
		Rule.ContributionRange.X,
		Rule.ContributionRange.Y,
		EvaluateCurve(Alpha, Rule.ResponseCurve)
	);
	const float Before = *Output;
	switch (Rule.CombinationMode)
	{
	case ELLMNPCModifierCombinationMode::Add:
		*Output += Contribution;
		break;
	case ELLMNPCModifierCombinationMode::Min:
		*Output = FMath::Min(*Output, Contribution);
		break;
	case ELLMNPCModifierCombinationMode::Max:
		*Output = FMath::Max(*Output, Contribution);
		break;
	case ELLMNPCModifierCombinationMode::OverrideIfAllowed:
		*Output = Contribution;
		break;
	default:
		*Output *= Contribution;
		break;
	}
	if (!FMath::IsNearlyEqual(Before, *Output))
	{
		AddStep(
			Trace,
			ResolveStageName(Rule.Stage),
			FName(*StaticEnum<ELLMNPCResolvedModifierField>()->GetNameStringByValue(
				static_cast<int64>(Rule.OutputModifier)
			)),
			FName(*StaticEnum<ELLMNPCModifierCombinationMode>()->GetNameStringByValue(
				static_cast<int64>(Rule.CombinationMode)
			)),
			Before,
			Contribution,
			*Output,
			FString::Printf(
				TEXT("%s=%.3f priority=%d"),
				*StaticEnum<ELLMNPCModifierInputField>()->GetNameStringByValue(
					static_cast<int64>(Rule.InputField)
				),
				Input,
				Rule.Priority
			)
		);
	}
}

void ClampValue(
	float& Value,
	const FVector2D& Range,
	FName Modifier,
	FLLMNPCModifierResolutionTrace& Trace
)
{
	const float Before = Value;
	Value = FMath::Clamp(Value, Range.X, Range.Y);
	if (!FMath::IsNearlyEqual(Before, Value))
	{
		AddStep(
			Trace,
			StageTemplateClamp,
			Modifier,
			TEXT("clamp"),
			Before,
			Value,
			Value,
			FString::Printf(TEXT("range=%.3f..%.3f"), Range.X, Range.Y)
		);
	}
}

FVector2D IntersectRange(
	const FVector2D& PolicyRange,
	const FVector2D& ContextRange
)
{
	if (ContextRange.X <= 0.0f || ContextRange.Y < ContextRange.X)
	{
		return PolicyRange;
	}
	const float Minimum = FMath::Max(PolicyRange.X, ContextRange.X);
	const float Maximum = FMath::Min(PolicyRange.Y, ContextRange.Y);
	return Maximum >= Minimum
		? FVector2D(Minimum, Maximum)
		: FVector2D::ZeroVector;
}

bool IsFinite(const FLLMNPCResolvedMotionModifiers& Modifiers)
{
	return
		FMath::IsFinite(Modifiers.Amplitude) &&
		FMath::IsFinite(Modifiers.SpeedScale) &&
		FMath::IsFinite(Modifiers.DurationScale) &&
		FMath::IsFinite(Modifiers.ReachScale) &&
		FMath::IsFinite(Modifiers.HeightScale) &&
		FMath::IsFinite(Modifiers.LateralScale) &&
		FMath::IsFinite(Modifiers.GazeEngagement) &&
		FMath::IsFinite(Modifiers.PalmOrientationWeight) &&
		FMath::IsFinite(Modifiers.FingerPoseWeight) &&
		FMath::IsFinite(Modifiers.TorsoParticipation) &&
		FMath::IsFinite(Modifiers.BlendInScale) &&
		FMath::IsFinite(Modifiers.BlendOutScale);
}

void ScaleTrackValues(FLLMMotionTrack& Track, float Scale)
{
	Track.Amplitude *= Scale;
	Track.Offset *= Scale;
	for (FLLMMotionKeyFloat& Key : Track.FloatKeys)
	{
		Key.V *= Scale;
	}
}
}

bool FLLMNPCContextModifierResolver::UsesRightArm(
	const ULLMNPCMotionTemplate& MotionTemplate
)
{
	return
		ArrayContainsToken(MotionTemplate.Metadata.RequiredChannels, TEXT("right_arm")) ||
		ArrayContainsToken(MotionTemplate.Metadata.RequiredChannels, TEXT("right_hand")) ||
		TrackContainsToken(MotionTemplate, TEXT("right_")) ||
		NameContains(MotionTemplate.Metadata.TemplateId, TEXT(".right")) ||
		NameContains(MotionTemplate.Metadata.PublicActionId, TEXT(".right"));
}

bool FLLMNPCContextModifierResolver::UsesLeftArm(
	const ULLMNPCMotionTemplate& MotionTemplate
)
{
	return
		ArrayContainsToken(MotionTemplate.Metadata.RequiredChannels, TEXT("left_arm")) ||
		ArrayContainsToken(MotionTemplate.Metadata.RequiredChannels, TEXT("left_hand")) ||
		TrackContainsToken(MotionTemplate, TEXT("left_")) ||
		NameContains(MotionTemplate.Metadata.TemplateId, TEXT(".left")) ||
		NameContains(MotionTemplate.Metadata.PublicActionId, TEXT(".left"));
}

bool FLLMNPCContextModifierResolver::UsesFineHandMotion(
	const ULLMNPCMotionTemplate& MotionTemplate
)
{
	return
		ArrayContainsToken(MotionTemplate.Metadata.BodyRegionTags, TEXT("hand")) ||
		ArrayContainsToken(MotionTemplate.Metadata.BodyRegionTags, TEXT("finger")) ||
		ArrayContainsToken(MotionTemplate.Metadata.RequiredChannels, TEXT("hand")) ||
		TrackContainsToken(MotionTemplate, TEXT("fingers."));
}

bool FLLMNPCContextModifierResolver::Resolve(
	const ULLMNPCMotionTemplate& MotionTemplate,
	const FLLMNPCTemplateModifiers& RequestedModifiers,
	const FLLMNPCSelectionContextSnapshot& SelectionContext,
	const FLLMNPCExecutionContextSnapshot& ExecutionContext,
	const ULLMNPCModifierMappingProfile* MappingProfile,
	const ULLMNPCSkeletonProfile* SkeletonProfile,
	FLLMNPCResolvedMotionModifiers& OutModifiers,
	FLLMNPCModifierResolutionTrace& OutTrace,
	FString& OutError
)
{
	OutModifiers = FLLMNPCResolvedMotionModifiers();
	OutTrace = FLLMNPCModifierResolutionTrace();
	OutError.Reset();
	if (
		!ExecutionContext.IsFinite() ||
		!FMath::IsFinite(RequestedModifiers.Amplitude) ||
		!FMath::IsFinite(RequestedModifiers.SpeedScale) ||
		!FMath::IsFinite(RequestedModifiers.DurationScale)
	)
	{
		OutError = TEXT("LLMNPC_MODIFIER_CONTEXT_NON_FINITE");
		return false;
	}

	const FLLMNPCModifierPolicy& Policy = MotionTemplate.ModifierPolicy;
	OutModifiers.Amplitude = RequestedModifiers.Amplitude;
	OutModifiers.SpeedScale = RequestedModifiers.SpeedScale;
	OutModifiers.DurationScale = RequestedModifiers.DurationScale;
	OutModifiers.bMirror = RequestedModifiers.bMirror;
	OutModifiers.TargetRef = RequestedModifiers.TargetRef.TrimStartAndEnd();
	OutModifiers.Style = RequestedModifiers.Style.IsNone()
		? FName(TEXT("neutral"))
		: RequestedModifiers.Style;
	OutModifiers.RandomSeed = RequestedModifiers.RandomSeed;
	AddStep(
		OutTrace,
		StageTemplateDefault,
		TEXT("all"),
		TEXT("initialize"),
		1.0f,
		1.0f,
		1.0f,
		TEXT("legacy-compatible defaults")
	);
	AddStep(
		OutTrace,
		StageModelRequest,
		TEXT("amplitude"),
		TEXT("set"),
		1.0f,
		RequestedModifiers.Amplitude,
		OutModifiers.Amplitude,
		TEXT("validated model request")
	);
	AddStep(
		OutTrace,
		StageModelRequest,
		TEXT("speed_scale"),
		TEXT("set"),
		1.0f,
		RequestedModifiers.SpeedScale,
		OutModifiers.SpeedScale,
		TEXT("validated model request")
	);
	AddStep(
		OutTrace,
		StageModelRequest,
		TEXT("duration_scale"),
		TEXT("set"),
		1.0f,
		RequestedModifiers.DurationScale,
		OutModifiers.DurationScale,
		TEXT("validated model request")
	);
	if (
		RequestedModifiers.bMirror ||
		!OutModifiers.TargetRef.IsEmpty() ||
		OutModifiers.Style != TEXT("neutral")
	)
	{
		AddStep(
			OutTrace,
			StageModelRequest,
			TEXT("qualifiers"),
			TEXT("set"),
			0.0f,
			0.0f,
			0.0f,
			FString::Printf(
				TEXT("style=%s mirror=%s target=%s"),
				*OutModifiers.Style.ToString(),
				RequestedModifiers.bMirror ? TEXT("true") : TEXT("false"),
				OutModifiers.TargetRef.IsEmpty()
					? TEXT("none")
					: *OutModifiers.TargetRef
			)
		);
	}

	if (
		!Policy.AllowedStyleTags.IsEmpty() &&
		!Policy.AllowedStyleTags.Contains(OutModifiers.Style)
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_STYLE_FORBIDDEN");
		return false;
	}
	if (OutModifiers.bMirror && !Policy.bAllowMirror)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_MIRROR_FORBIDDEN");
		return false;
	}
	if (
		MotionTemplate.Metadata.bRequiresTarget &&
		(OutModifiers.TargetRef.IsEmpty() || !ExecutionContext.Target.bValid)
	)
	{
		SetFallback(
			TEXT("LLMNPC_MODIFIER_TARGET_UNAVAILABLE"),
			TEXT("required target is missing or invalid"),
			OutModifiers,
			OutTrace,
			StageTarget
		);
		return true;
	}

	const FLLMNPCStylePreset Style =
		ULLMNPCStyleResolver::GetBuiltInPreset(OutModifiers.Style);
	Multiply(
		OutModifiers.Amplitude,
		Style.AmplitudeScale,
		StageStylePreset,
		TEXT("amplitude"),
		OutModifiers.Style.ToString(),
		OutTrace
	);
	Multiply(
		OutModifiers.SpeedScale,
		Style.SpeedScale,
		StageStylePreset,
		TEXT("speed_scale"),
		OutModifiers.Style.ToString(),
		OutTrace
	);
	Multiply(
		OutModifiers.DurationScale,
		Style.DurationScale,
		StageStylePreset,
		TEXT("duration_scale"),
		OutModifiers.Style.ToString(),
		OutTrace
	);
	OutModifiers.GazeEngagement = Style.GazeEngagement;
	AddStep(
		OutTrace,
		StageStylePreset,
		TEXT("gaze_engagement"),
		TEXT("set"),
		1.0f,
		Style.GazeEngagement,
		OutModifiers.GazeEngagement,
		OutModifiers.Style.ToString()
	);

	TArray<FLLMNPCModifierMappingRule> DefaultRules;
	const TArray<FLLMNPCModifierMappingRule>* Rules = nullptr;
	if (MappingProfile)
	{
		FString MappingError;
		if (!MappingProfile->Validate(MappingError))
		{
			OutError = MappingError;
			return false;
		}
		Rules = &MappingProfile->Rules;
	}
	else
	{
		DefaultRules = ULLMNPCModifierMappingProfile::BuildMannyDefaultRules();
		Rules = &DefaultRules;
	}

	for (const ELLMNPCModifierMappingStage Stage : {
		ELLMNPCModifierMappingStage::Personality,
		ELLMNPCModifierMappingStage::Emotion,
		ELLMNPCModifierMappingStage::Relationship})
	{
		TArray<const FLLMNPCModifierMappingRule*> StageRules;
		for (const FLLMNPCModifierMappingRule& Rule : *Rules)
		{
			if (Rule.Stage == Stage)
			{
				StageRules.Add(&Rule);
			}
		}
		StageRules.Sort(
			[](const FLLMNPCModifierMappingRule& A, const FLLMNPCModifierMappingRule& B)
			{
				return A.Priority < B.Priority;
			}
		);
		for (const FLLMNPCModifierMappingRule* Rule : StageRules)
		{
			ApplyMappingRule(*Rule, SelectionContext, OutModifiers, OutTrace);
		}
	}

	const bool bUsesRightArm = UsesRightArm(MotionTemplate);
	const bool bUsesLeftArm = UsesLeftArm(MotionTemplate);
	const bool bUsesArm = bUsesRightArm || bUsesLeftArm;
	for (const FName ActiveState : SelectionContext.ActiveStates)
	{
		if (
			MotionTemplate.Metadata.BlockedStates.Contains(ActiveState) &&
			ActiveState != TEXT("right_hand_busy") &&
			ActiveState != TEXT("left_hand_busy")
		)
		{
			SetFallback(
				TEXT("LLMNPC_MODIFIER_GAMEPLAY_STATE_BLOCKED"),
				FString::Printf(
					TEXT("template blocked by gameplay state %s"),
					*ActiveState.ToString()
				),
				OutModifiers,
				OutTrace
			);
			return true;
		}
	}
	if (
		ExecutionContext.bUpperBodyOccupied &&
		(bUsesArm || MotionTemplate.Metadata.BodyRegionTags.Contains(TEXT("torso")))
	)
	{
		SetFallback(
			TEXT("LLMNPC_MODIFIER_UPPER_BODY_OCCUPIED"),
			TEXT("upper body is reserved by gameplay"),
			OutModifiers,
			OutTrace
		);
		return true;
	}
	if (
		ExecutionContext.bRightHandOccupied &&
		ExecutionContext.bLeftHandOccupied &&
		bUsesArm
	)
	{
		SetFallback(
			TEXT("LLMNPC_MODIFIER_BOTH_HANDS_OCCUPIED"),
			TEXT("both hands are occupied"),
			OutModifiers,
			OutTrace
		);
		return true;
	}
	if (
		ExecutionContext.bRightHandOccupied &&
		bUsesRightArm &&
		!OutModifiers.bMirror
	)
	{
		if (Policy.bAllowMirror && !ExecutionContext.bLeftHandOccupied)
		{
			OutModifiers.bMirror = true;
			AddStep(
				OutTrace,
				StageMovement,
				TEXT("mirror"),
				TEXT("mirror"),
				0.0f,
				1.0f,
				1.0f,
				TEXT("right hand occupied; mirrored to left")
			);
		}
		else
		{
			SetFallback(
				TEXT("LLMNPC_MODIFIER_OCCUPIED_HAND_NO_VARIANT"),
				TEXT("right hand occupied and selected variant cannot mirror"),
				OutModifiers,
				OutTrace
			);
			return true;
		}
	}
	if (
		ExecutionContext.bLeftHandOccupied &&
		bUsesRightArm &&
		OutModifiers.bMirror
	)
	{
		OutModifiers.bMirror = false;
		AddStep(
			OutTrace,
			StageMovement,
			TEXT("mirror"),
			TEXT("mirror"),
			1.0f,
			0.0f,
			0.0f,
			TEXT("left hand occupied; retained right-side source")
		);
	}

	if (
		ExecutionContext.MovementMode == ELLMNPCExecutionMovementMode::Running &&
		(bUsesArm || UsesFineHandMotion(MotionTemplate))
	)
	{
		SetFallback(
			TEXT("LLMNPC_MODIFIER_RUNNING_FINE_GESTURE_FORBIDDEN"),
			TEXT("running forbids fine upper-body gestures"),
			OutModifiers,
			OutTrace
		);
		return true;
	}
	if (
		ExecutionContext.MovementMode == ELLMNPCExecutionMovementMode::Turning &&
		MotionTemplate.Metadata.bRequiresTarget
	)
	{
		SetFallback(
			TEXT("LLMNPC_MODIFIER_DELAY_FOR_TURNING"),
			TEXT("targeted gesture waits for a stable facing direction"),
			OutModifiers,
			OutTrace
		);
		return true;
	}
	if (
		ExecutionContext.MovementMode == ELLMNPCExecutionMovementMode::Walking &&
		!MotionTemplate.Metadata.bCanRunWhileMoving
	)
	{
		SetFallback(
			TEXT("LLMNPC_MODIFIER_MOVEMENT_INCOMPATIBLE"),
			TEXT("template is not allowed while moving"),
			OutModifiers,
			OutTrace
		);
		return true;
	}
	if (ExecutionContext.MovementMode == ELLMNPCExecutionMovementMode::Walking)
	{
		Multiply(
			OutModifiers.Amplitude,
			0.82f,
			StageMovement,
			TEXT("amplitude"),
			TEXT("walking stability"),
			OutTrace
		);
		Multiply(
			OutModifiers.ReachScale,
			0.85f,
			StageMovement,
			TEXT("reach_scale"),
			TEXT("walking stability"),
			OutTrace
		);
		Multiply(
			OutModifiers.TorsoParticipation,
			0.65f,
			StageMovement,
			TEXT("torso_participation"),
			TEXT("locomotion owns the base torso"),
			OutTrace
		);
	}

	if (ExecutionContext.Target.bValid)
	{
		if (ExecutionContext.Target.DistanceCm < 120.0f)
		{
			Multiply(
				OutModifiers.Amplitude,
				0.85f,
				StageTarget,
				TEXT("amplitude"),
				TEXT("near target"),
				OutTrace
			);
			Multiply(
				OutModifiers.ReachScale,
				0.78f,
				StageTarget,
				TEXT("reach_scale"),
				TEXT("near target"),
				OutTrace
			);
		}
		else if (
			ExecutionContext.Target.DistanceCm > 350.0f &&
			MotionTemplate.Metadata.bRequiresTarget
		)
		{
			Multiply(
				OutModifiers.Amplitude,
				1.05f,
				StageTarget,
				TEXT("amplitude"),
				TEXT("far target clarity"),
				OutTrace
			);
			Multiply(
				OutModifiers.ReachScale,
				1.08f,
				StageTarget,
				TEXT("reach_scale"),
				TEXT("far target clarity"),
				OutTrace
			);
		}
		const float AbsoluteHeight = FMath::Abs(
			ExecutionContext.Target.HeightRelativeCm
		);
		if (AbsoluteHeight > 100.0f)
		{
			Multiply(
				OutModifiers.HeightScale,
				FMath::Clamp(100.0f / AbsoluteHeight, 0.55f, 1.0f),
				StageTarget,
				TEXT("height_scale"),
				TEXT("target height clamped toward reachable space"),
				OutTrace
			);
		}
	}

	const float Space = FMath::Clamp(ExecutionContext.AvailableSpace, 0.0f, 1.0f);
	if (Policy.bEnableObstacleAdaptation && Space < 1.0f)
	{
		Multiply(
			OutModifiers.Amplitude,
			FMath::Lerp(Policy.MinObstacleAmplitudeScale, 1.0f, Space),
			StageObstacle,
			TEXT("amplitude"),
			TEXT("available-space reduction"),
			OutTrace
		);
		Multiply(
			OutModifiers.ReachScale,
			FMath::Lerp(Policy.MinObstacleReachScale, 1.0f, Space),
			StageObstacle,
			TEXT("reach_scale"),
			TEXT("available-space reduction"),
			OutTrace
		);
	}

	if (Policy.bEnableObstacleAdaptation && bUsesArm)
	{
		const FLLMNPCObstacleSweepResult& SelectedObstacle =
			OutModifiers.bMirror
			? ExecutionContext.LeftObstacle
			: ExecutionContext.RightObstacle;
		const FLLMNPCObstacleSweepResult& OppositeObstacle =
			OutModifiers.bMirror
			? ExecutionContext.RightObstacle
			: ExecutionContext.LeftObstacle;
		if (SelectedObstacle.bTested && SelectedObstacle.bBlockingHit)
		{
			if (
				Policy.bAllowMirror &&
				OppositeObstacle.bTested &&
				OppositeObstacle.Clearance > SelectedObstacle.Clearance + 0.2f &&
				!(OutModifiers.bMirror
					? ExecutionContext.bRightHandOccupied
					: ExecutionContext.bLeftHandOccupied)
			)
			{
				OutModifiers.bMirror = !OutModifiers.bMirror;
				AddStep(
					OutTrace,
					StageObstacle,
					TEXT("mirror"),
					TEXT("mirror"),
					OutModifiers.bMirror ? 0.0f : 1.0f,
					1.0f,
					OutModifiers.bMirror ? 1.0f : 0.0f,
					TEXT("opposite side has greater clearance")
				);
			}
			else if (SelectedObstacle.Clearance <= Policy.ObstacleCancelClearance)
			{
				SetFallback(
					TEXT("LLMNPC_MODIFIER_OBSTACLE_BLOCKED"),
					TEXT("insufficient arm clearance"),
					OutModifiers,
					OutTrace,
					StageObstacle
				);
				return true;
			}
			else
			{
				const float Clearance = FMath::Clamp(
					SelectedObstacle.Clearance,
					0.0f,
					1.0f
				);
				Multiply(
					OutModifiers.Amplitude,
					FMath::Lerp(
						Policy.MinObstacleAmplitudeScale,
						1.0f,
						Clearance
					),
					StageObstacle,
					TEXT("amplitude"),
					TEXT("arm sweep"),
					OutTrace
				);
				Multiply(
					OutModifiers.ReachScale,
					FMath::Lerp(
						Policy.MinObstacleReachScale,
						1.0f,
						Clearance
					),
					StageObstacle,
					TEXT("reach_scale"),
					TEXT("arm sweep"),
					OutTrace
				);
			}
		}
	}

	const FVector2D AmplitudeRange = IntersectRange(
		Policy.AmplitudeRange,
		RequestedModifiers.ContextAmplitudeRange
	);
	const FVector2D SpeedRange = IntersectRange(
		Policy.SpeedRange,
		RequestedModifiers.ContextSpeedRange
	);
	const FVector2D DurationRange = IntersectRange(
		Policy.DurationRange,
		RequestedModifiers.ContextDurationRange
	);
	if (
		AmplitudeRange.X <= 0.0f ||
		SpeedRange.X <= 0.0f ||
		DurationRange.X <= 0.0f
	)
	{
		OutError = TEXT("LLMNPC_TEMPLATE_CONTEXT_POLICY_INCOMPATIBLE");
		return false;
	}

	ClampValue(OutModifiers.Amplitude, AmplitudeRange, TEXT("amplitude"), OutTrace);
	ClampValue(OutModifiers.SpeedScale, SpeedRange, TEXT("speed_scale"), OutTrace);
	ClampValue(OutModifiers.DurationScale, DurationRange, TEXT("duration_scale"), OutTrace);
	ClampValue(OutModifiers.ReachScale, Policy.ReachScaleRange, TEXT("reach_scale"), OutTrace);
	ClampValue(OutModifiers.HeightScale, Policy.HeightScaleRange, TEXT("height_scale"), OutTrace);
	ClampValue(OutModifiers.LateralScale, Policy.LateralScaleRange, TEXT("lateral_scale"), OutTrace);
	ClampValue(
		OutModifiers.GazeEngagement,
		Policy.GazeEngagementRange,
		TEXT("gaze_engagement"),
		OutTrace
	);
	ClampValue(
		OutModifiers.PalmOrientationWeight,
		Policy.PalmOrientationWeightRange,
		TEXT("palm_orientation_weight"),
		OutTrace
	);
	ClampValue(
		OutModifiers.FingerPoseWeight,
		Policy.FingerPoseWeightRange,
		TEXT("finger_pose_weight"),
		OutTrace
	);
	ClampValue(
		OutModifiers.TorsoParticipation,
		Policy.TorsoParticipationRange,
		TEXT("torso_participation"),
		OutTrace
	);
	ClampValue(
		OutModifiers.BlendInScale,
		Policy.BlendInScaleRange,
		TEXT("blend_in_scale"),
		OutTrace
	);
	ClampValue(
		OutModifiers.BlendOutScale,
		Policy.BlendOutScaleRange,
		TEXT("blend_out_scale"),
		OutTrace
	);
	if (Policy.CycleCountRange.Y > 0)
	{
		OutModifiers.CycleCount = FMath::Clamp(
			OutModifiers.CycleCount > 0
				? OutModifiers.CycleCount
				: Policy.CycleCountRange.X,
			Policy.CycleCountRange.X,
			Policy.CycleCountRange.Y
		);
	}

	if (SkeletonProfile && ExecutionContext.Target.bValid)
	{
		const FVector BoundsMax =
			SkeletonProfile->UpperBodyConstraints.HandReachBoundsMaxCS;
		const FVector BoundsMin =
			SkeletonProfile->UpperBodyConstraints.HandReachBoundsMinCS;
		const float MaximumHeight = FMath::Max(
			FMath::Abs(BoundsMin.Z),
			FMath::Abs(BoundsMax.Z)
		);
		const float AbsoluteHeight = FMath::Abs(
			ExecutionContext.Target.HeightRelativeCm
		);
		if (MaximumHeight > KINDA_SMALL_NUMBER && AbsoluteHeight > MaximumHeight)
		{
			const float Before = OutModifiers.HeightScale;
			OutModifiers.HeightScale = FMath::Min(
				OutModifiers.HeightScale,
				MaximumHeight / AbsoluteHeight
			);
			if (!FMath::IsNearlyEqual(Before, OutModifiers.HeightScale))
			{
				AddStep(
					OutTrace,
					StageSkeletonClamp,
					TEXT("height_scale"),
					TEXT("clamp"),
					Before,
					MaximumHeight,
					OutModifiers.HeightScale,
					TEXT("Manny hand reach bounds")
				);
			}
		}
	}

	if (!IsFinite(OutModifiers))
	{
		OutError = TEXT("LLMNPC_MODIFIER_RESULT_NON_FINITE");
		return false;
	}
	OutModifiers.ResultCode = TEXT("LLMNPC_MODIFIER_RESOLVED");
	OutTrace.ResultCode = OutModifiers.ResultCode;
	return true;
}

void FLLMNPCContextModifierResolver::ApplyToCompiledPlan(
	const ULLMNPCMotionTemplate& MotionTemplate,
	const FLLMNPCResolvedMotionModifiers& Modifiers,
	FLLMMotionPlan& InOutPlan
)
{
	InOutPlan.Clip.BlendIn *= Modifiers.BlendInScale;
	InOutPlan.Clip.BlendOut *= Modifiers.BlendOutScale;
	for (FLLMMotionTrack& Track : InOutPlan.Clip.Tracks)
	{
		const FString Control = Track.ControlId.ToString();
		if (Track.TrackType == ELLMMotionTrackType::IKReach)
		{
			Track.Reach = FMath::Clamp(
				Track.Reach * Modifiers.ReachScale,
				0.0f,
				1.0f
			);
		}
		if (
			Track.TrackType == ELLMMotionTrackType::LookAt ||
			Control == TEXT("gaze.target")
		)
		{
			Track.Strength *= Modifiers.GazeEngagement;
		}
		if (Control.Contains(TEXT("palm_target")))
		{
			Track.Strength *= Modifiers.PalmOrientationWeight;
		}
		if (Control.Contains(TEXT("fingers.")))
		{
			Track.Strength *= Modifiers.FingerPoseWeight;
		}
		if (Control.StartsWith(TEXT("chest.")))
		{
			ScaleTrackValues(Track, Modifiers.TorsoParticipation);
		}
		if (
			Modifiers.CycleCount > 0 &&
			Track.TrackType == ELLMMotionTrackType::Oscillator &&
			InOutPlan.Clip.Duration > KINDA_SMALL_NUMBER
		)
		{
			Track.Frequency =
				static_cast<float>(Modifiers.CycleCount) /
				InOutPlan.Clip.Duration;
		}
	}
	static_cast<void>(MotionTemplate);
}
