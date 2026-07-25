#include "Quality/LLMNPCKinematicValidator.h"

#include "LLMNPCControlManifest.h"
#include "LLMNPCMotionSampler.h"
#include "Misc/SecureHash.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

namespace
{
struct FLLMNPCKinematicSample
{
	float Time = 0.0f;
	double Value = 0.0;
};

struct FLLMNPCKinematicPeak
{
	double Value = 0.0;
	float Time = 0.0f;
};

float KinematicSmooth01(float Value)
{
	const float T = FMath::Clamp(Value, 0.0f, 1.0f);
	return T * T * (3.0f - 2.0f * T);
}

float EvaluateKinematicClipAlpha(const FLLMMotionClip& Clip, float Time)
{
	const float InAlpha = Clip.BlendIn > KINDA_SMALL_NUMBER
		? FMath::Clamp(Time / Clip.BlendIn, 0.0f, 1.0f)
		: 1.0f;
	const float OutAlpha = Clip.BlendOut > KINDA_SMALL_NUMBER
		? FMath::Clamp((Clip.Duration - Time) / Clip.BlendOut, 0.0f, 1.0f)
		: 1.0f;
	return KinematicSmooth01(FMath::Min(InAlpha, OutAlpha));
}

const FLLMControlDefinition* FindKinematicControl(
	const ULLMNPCControlManifest* Manifest,
	FName ControlId
)
{
	return Manifest
		? Manifest->FindControl(ControlId)
		: ULLMNPCControlManifest::FindBuiltInControl(ControlId);
}

FName NormalizeConstraintControlId(FName ControlId)
{
	const FString Value = ControlId.ToString();
	if (Value.StartsWith(TEXT("mirror_left_")))
	{
		return FName(*Value.RightChop(7));
	}
	return ControlId;
}

bool ResolveAxisConstraint(
	const ULLMNPCSkeletonProfile& Profile,
	FName ControlId,
	double& OutMin,
	double& OutMax
)
{
	FString Control = NormalizeConstraintControlId(ControlId).ToString();
	FName Semantic = NAME_None;
	if (Control.StartsWith(TEXT("head.")))
	{
		Semantic = TEXT("head");
	}
	else if (Control.StartsWith(TEXT("chest.")))
	{
		Semantic = TEXT("chest");
	}
	else if (Control.StartsWith(TEXT("right_shoulder.")))
	{
		Semantic = TEXT("shoulder_right");
	}
	else if (Control.StartsWith(TEXT("left_shoulder.")))
	{
		Semantic = TEXT("shoulder_left");
	}
	else if (Control.StartsWith(TEXT("right_upperarm.")))
	{
		Semantic = TEXT("upperarm_right");
	}
	else if (Control.StartsWith(TEXT("left_upperarm.")))
	{
		Semantic = TEXT("upperarm_left");
	}
	else if (Control.StartsWith(TEXT("right_lowerarm.")))
	{
		Semantic = TEXT("lowerarm_right");
	}
	else if (Control.StartsWith(TEXT("left_lowerarm.")))
	{
		Semantic = TEXT("lowerarm_left");
	}
	else if (Control.StartsWith(TEXT("right_hand.")))
	{
		Semantic = TEXT("hand_right");
	}
	else if (Control.StartsWith(TEXT("left_hand.")))
	{
		Semantic = TEXT("hand_left");
	}
	if (Semantic.IsNone())
	{
		return false;
	}

	const FLLMNPCBoneAxisBasis* Basis = Profile.AxisBases.Find(Semantic);
	if (!Basis)
	{
		return false;
	}

	if (Control.EndsWith(TEXT(".pitch")))
	{
		OutMin = Basis->MinAdditiveRotation.Pitch;
		OutMax = Basis->MaxAdditiveRotation.Pitch;
		return true;
	}
	if (Control.EndsWith(TEXT(".yaw")))
	{
		OutMin = Basis->MinAdditiveRotation.Yaw;
		OutMax = Basis->MaxAdditiveRotation.Yaw;
		return true;
	}
	if (Control.EndsWith(TEXT(".roll")))
	{
		OutMin = Basis->MinAdditiveRotation.Roll;
		OutMax = Basis->MaxAdditiveRotation.Roll;
		return true;
	}
	return false;
}

const FLLMNPCIKChainProfile* FindIKChainForControl(
	const ULLMNPCSkeletonProfile& Profile,
	FName ControlId
)
{
	FName ChainId = NAME_None;
	if (ControlId == TEXT("right_hand.ik"))
	{
		ChainId = TEXT("right_arm");
	}
	else if (ControlId == TEXT("left_hand.ik"))
	{
		ChainId = TEXT("left_arm");
	}
	if (ChainId.IsNone())
	{
		return nullptr;
	}
	return Profile.IKChains.FindByPredicate(
		[ChainId](const FLLMNPCIKChainProfile& Chain)
		{
			return Chain.ChainId == ChainId;
		}
	);
}

void AddKinematicIssue(
	FLLMNPCKinematicQualityReport& Report,
	const TCHAR* Code,
	ELLMNPCKinematicIssueSeverity Severity,
	const FString& FieldPath,
	float SampleTime,
	double Observed,
	double Limit,
	const FString& Message
)
{
	FLLMNPCKinematicValidationIssue& Issue = Report.Issues.AddDefaulted_GetRef();
	Issue.Code = Code;
	Issue.Severity = Severity;
	Issue.FieldPath = FieldPath;
	Issue.SampleTimeSeconds = SampleTime;
	Issue.ObservedValue = Observed;
	Issue.LimitValue = Limit;
	Issue.Message = Message;
}

TArray<float> BuildUniformSampleTimes(
	const FLLMMotionClip& Clip,
	const FLLMNPCKinematicValidationSettings& Settings
)
{
	TArray<float> Times;
	const float Duration = FMath::Max(0.0f, Clip.Duration);
	const float Rate = FMath::Clamp(Settings.SampleRateHz, 1.0f, 240.0f);
	const int32 UniformSampleCount = FMath::Min(
		Settings.MaxSamples,
		FMath::Max(2, FMath::CeilToInt(Duration * Rate) + 1)
	);
	for (int32 Index = 0; Index < UniformSampleCount; ++Index)
	{
		Times.Add(
			UniformSampleCount > 1
			? Duration * static_cast<float>(Index) /
				static_cast<float>(UniformSampleCount - 1)
			: 0.0f
		);
	}
	return Times;
}

TArray<float> BuildSampleTimes(
	const FLLMMotionClip& Clip,
	const FLLMNPCKinematicValidationSettings& Settings
)
{
	TArray<float> Times = BuildUniformSampleTimes(Clip, Settings);
	const float Duration = FMath::Max(0.0f, Clip.Duration);
	Times.Add(FMath::Clamp(Clip.BlendIn, 0.0f, Duration));
	Times.Add(FMath::Clamp(Duration - Clip.BlendOut, 0.0f, Duration));
	for (const FLLMMotionTrack& Track : Clip.Tracks)
	{
		Times.Add(FMath::Clamp(Track.StartTime, 0.0f, Duration));
		Times.Add(FMath::Clamp(Track.EndTime, 0.0f, Duration));
		for (const FLLMMotionKeyFloat& Key : Track.FloatKeys)
		{
			Times.Add(FMath::Clamp(Key.T, 0.0f, Duration));
		}
	}

	Times.Sort();
	for (int32 Index = Times.Num() - 1; Index > 0; --Index)
	{
		if (FMath::IsNearlyEqual(Times[Index], Times[Index - 1], 0.00001f))
		{
			Times.RemoveAt(Index);
		}
	}
	if (Times.Num() > Settings.MaxSamples)
	{
		Times.SetNum(Settings.MaxSamples);
	}
	return Times;
}

double EvaluateKinematicTrackValue(
	const FLLMMotionClip& Clip,
	const FLLMMotionTrack& Track,
	float Time
)
{
	return
		static_cast<double>(FLLMNPCMotionSampler::EvaluateFloatTrack(Track, Time)) *
		static_cast<double>(FLLMNPCMotionSampler::EvaluateEnvelope(Track, Time)) *
		static_cast<double>(EvaluateKinematicClipAlpha(Clip, Time)) *
		static_cast<double>(Track.Strength);
}

FLLMNPCKinematicPeak FindDerivativePeak(
	const TArray<FLLMNPCKinematicSample>& Samples,
	int32 DerivativeOrder
)
{
	TArray<FLLMNPCKinematicSample> Current = Samples;
	for (int32 Order = 0; Order < DerivativeOrder; ++Order)
	{
		TArray<FLLMNPCKinematicSample> Derivative;
		Derivative.Reserve(FMath::Max(0, Current.Num() - 1));
		for (int32 Index = 1; Index < Current.Num(); ++Index)
		{
			const double DeltaTime =
				static_cast<double>(Current[Index].Time - Current[Index - 1].Time);
			if (DeltaTime <= SMALL_NUMBER)
			{
				continue;
			}
			FLLMNPCKinematicSample& Sample = Derivative.AddDefaulted_GetRef();
			Sample.Time = Current[Index].Time;
			Sample.Value = (Current[Index].Value - Current[Index - 1].Value) / DeltaTime;
		}
		Current = MoveTemp(Derivative);
		if (Current.IsEmpty())
		{
			break;
		}
	}

	FLLMNPCKinematicPeak Peak;
	for (const FLLMNPCKinematicSample& Sample : Current)
	{
		const double AbsoluteValue = FMath::Abs(Sample.Value);
		if (AbsoluteValue > Peak.Value)
		{
			Peak.Value = AbsoluteValue;
			Peak.Time = Sample.Time;
		}
	}
	return Peak;
}

FString BuildPlanHash(const FLLMMotionPlan& Plan)
{
	FString Canonical = FString::Printf(
		TEXT("%s|%s|%s|%.9g|%.9g|%.9g|%.9g|%d\n"),
		*Plan.Version,
		*Plan.Intent,
		*Plan.Clip.ClipId,
		Plan.Clip.Duration,
		Plan.Clip.BlendIn,
		Plan.Clip.BlendOut,
		Plan.Clip.Priority,
		Plan.Clip.bInterruptible ? 1 : 0
	);
	for (const FLLMMotionTrack& Track : Plan.Clip.Tracks)
	{
		Canonical += FString::Printf(
			TEXT("%s|%d|%d|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%s|%s|%.9g,%.9g,%.9g\n"),
			*Track.ControlId.ToString(),
			static_cast<int32>(Track.TrackType),
			static_cast<int32>(Track.ValueType),
			Track.StartTime,
			Track.EndTime,
			Track.Amplitude,
			Track.Frequency,
			Track.Phase,
			Track.Strength,
			*Track.TargetRef,
			*Track.Anchor.ToString(),
			Track.Offset.X,
			Track.Offset.Y,
			Track.Offset.Z
		);
		for (const FLLMMotionKeyFloat& Key : Track.FloatKeys)
		{
			Canonical += FString::Printf(TEXT("%.9g:%.9g\n"), Key.T, Key.V);
		}
	}
	return FString::Printf(TEXT("md5:%s"), *FMD5::HashAnsiString(*Canonical));
}

FString BuildReportHash(const FLLMNPCKinematicQualityReport& Report)
{
	FString Canonical = FString::Printf(
		TEXT("%s|%s|%s|%s|%.9g|%d|%d\n"),
		*Report.SchemaVersion,
		*Report.ProfileId.ToString(),
		*Report.CapabilityHash,
		*Report.PlanHash,
		Report.SampleRateHz,
		Report.SampleCount,
		Report.bBaselineApproved ? 1 : 0
	);
	for (const FLLMNPCKinematicValidationIssue& Issue : Report.Issues)
	{
		Canonical += FString::Printf(
			TEXT("%s|%d|%s|%.9g|%.17g|%.17g\n"),
			*Issue.Code,
			static_cast<int32>(Issue.Severity),
			*Issue.FieldPath,
			Issue.SampleTimeSeconds,
			Issue.ObservedValue,
			Issue.LimitValue
		);
	}
	for (const FLLMNPCKinematicTrackMetrics& Metrics : Report.TrackMetrics)
	{
		Canonical += FString::Printf(
			TEXT("metric|%s|%.17g|%.17g|%.17g|%.17g|%.17g|%.17g\n"),
			*Metrics.ControlId.ToString(),
			Metrics.MaxAbsoluteValue,
			Metrics.MaxAbsoluteSpeed,
			Metrics.MaxAbsoluteAcceleration,
			Metrics.MaxAbsoluteJerk,
			Metrics.StartAbsoluteValue,
			Metrics.EndAbsoluteValue
		);
	}
	return FString::Printf(TEXT("md5:%s"), *FMD5::HashAnsiString(*Canonical));
}
}

FLLMNPCKinematicQualityReport FLLMNPCKinematicValidator::ValidatePlan(
	const FLLMMotionPlan& Plan,
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* ControlManifest,
	const FString& CapabilityHash,
	const FLLMNPCKinematicValidationSettings& Settings
)
{
	FLLMNPCKinematicQualityReport Report;
	Report.ProfileId = Profile.ProfileId;
	Report.CapabilityHash = CapabilityHash;
	Report.PlanHash = BuildPlanHash(Plan);
	Report.SampleRateHz = FMath::Clamp(Settings.SampleRateHz, 1.0f, 240.0f);
	Report.bBaselineApproved =
		Profile.UpperBodyConstraints.bKinematicBaselineApproved;

	const TArray<float> SampleTimes = BuildSampleTimes(Plan.Clip, Settings);
	const TArray<float> DerivativeSampleTimes =
		BuildUniformSampleTimes(Plan.Clip, Settings);
	Report.SampleCount = SampleTimes.Num();
	if (!Report.bBaselineApproved)
	{
		AddKinematicIssue(
			Report,
			TEXT("LLMNPC_KINEMATIC_BASELINE_NOT_APPROVED"),
			ELLMNPCKinematicIssueSeverity::Diagnostic,
			TEXT("profile.upper_body_constraints.validation_baseline"),
			0.0f,
			0.0,
			1.0,
			TEXT("Kinematic findings are diagnostic until the Manny baseline is approved.")
		);
	}

	for (int32 TrackIndex = 0; TrackIndex < Plan.Clip.Tracks.Num(); ++TrackIndex)
	{
		const FLLMMotionTrack& Track = Plan.Clip.Tracks[TrackIndex];
		const FString FieldPath = FString::Printf(
			TEXT("clip.tracks[%d].%s"),
			TrackIndex,
			*Track.ControlId.ToString()
		);
		const FLLMControlDefinition* Control = FindKinematicControl(
			ControlManifest,
			Track.ControlId
		);
		if (!Control)
		{
			continue;
		}

		if (Control->SolverType == ELLMControlSolverType::TwoBoneIK)
		{
			if (const FLLMNPCIKChainProfile* Chain = FindIKChainForControl(Profile, Track.ControlId))
			{
				if (
					!FMath::IsFinite(Track.Reach) ||
					Track.Reach < Chain->MinReachScale ||
					Track.Reach > Chain->MaxReachScale
				)
				{
					const double Limit = Track.Reach < Chain->MinReachScale
						? Chain->MinReachScale
						: Chain->MaxReachScale;
					AddKinematicIssue(
						Report,
						TEXT("LLMNPC_KINEMATIC_IK_OVERREACH"),
						ELLMNPCKinematicIssueSeverity::Error,
						FieldPath + TEXT(".reach"),
						Track.StartTime,
						Track.Reach,
						Limit,
						TEXT("IK reach lies outside the Skeleton Profile envelope.")
					);
				}
			}
		}

		if (
			Control->SolverType != ELLMControlSolverType::AdditiveRotation &&
			Control->SolverType != ELLMControlSolverType::LocalOffset &&
			Control->SolverType != ELLMControlSolverType::FingerPoseBlend
		)
		{
			continue;
		}

		TArray<FLLMNPCKinematicSample> Samples;
		Samples.Reserve(SampleTimes.Num());
		TArray<FLLMNPCKinematicSample> DerivativeSamples;
		DerivativeSamples.Reserve(DerivativeSampleTimes.Num());
		double JointMin = Control->MinValue;
		double JointMax = Control->MaxValue;
		double ProfileMin = 0.0;
		double ProfileMax = 0.0;
		if (
			Control->SolverType == ELLMControlSolverType::AdditiveRotation &&
			ResolveAxisConstraint(Profile, Track.ControlId, ProfileMin, ProfileMax)
		)
		{
			JointMin = FMath::Max(JointMin, ProfileMin);
			JointMax = FMath::Min(JointMax, ProfileMax);
		}

		bool bJointLimitReported = false;
		for (const float SampleTime : SampleTimes)
		{
			const double Value = EvaluateKinematicTrackValue(
				Plan.Clip,
				Track,
				SampleTime
			);
			FLLMNPCKinematicSample& Sample = Samples.AddDefaulted_GetRef();
			Sample.Time = SampleTime;
			Sample.Value = Value;
			if (
				!bJointLimitReported &&
				(
					!FMath::IsFinite(Value) ||
					JointMin > JointMax ||
					Value < JointMin - KINDA_SMALL_NUMBER ||
					Value > JointMax + KINDA_SMALL_NUMBER
				)
			)
			{
				const double Limit = Value < JointMin ? JointMin : JointMax;
				AddKinematicIssue(
					Report,
					TEXT("LLMNPC_KINEMATIC_JOINT_LIMIT"),
					ELLMNPCKinematicIssueSeverity::Error,
					FieldPath,
					SampleTime,
					Value,
					Limit,
					TEXT("Sampled value lies outside the effective Control/Profile range.")
				);
				bJointLimitReported = true;
			}
		}
		for (const float SampleTime : DerivativeSampleTimes)
		{
			FLLMNPCKinematicSample& Sample =
				DerivativeSamples.AddDefaulted_GetRef();
			Sample.Time = SampleTime;
			Sample.Value = EvaluateKinematicTrackValue(
				Plan.Clip,
				Track,
				SampleTime
			);
		}

		const FName ConstraintId = NormalizeConstraintControlId(Track.ControlId);
		const FLLMNPCKinematicControlConstraint* Constraint =
			Profile.FindControlConstraint(ConstraintId);
		if (!Constraint)
		{
			AddKinematicIssue(
				Report,
				TEXT("LLMNPC_KINEMATIC_CONSTRAINT_MISSING"),
				ELLMNPCKinematicIssueSeverity::Warning,
				FieldPath,
				0.0f,
				0.0,
				0.0,
				TEXT("No calibrated derivative limits exist for this control.")
			);
			continue;
		}

		const bool bAngular =
			Control->SolverType == ELLMControlSolverType::AdditiveRotation;
		const bool bNormalized =
			Control->SolverType == ELLMControlSolverType::FingerPoseBlend;
		const double SpeedLimit = bAngular
			? Constraint->MaxAngularSpeedDegreesPerSecond
			: bNormalized
				? Constraint->MaxNormalizedSpeedPerSecond
				: Constraint->MaxPositionSpeedCentimetersPerSecond;
		const double AccelerationLimit = bAngular
			? Constraint->MaxAngularAccelerationDegreesPerSecondSquared
			: bNormalized
				? Constraint->MaxNormalizedAccelerationPerSecondSquared
				: Constraint->MaxPositionAccelerationCentimetersPerSecondSquared;
		const double JerkLimit = bAngular
			? Constraint->MaxAngularJerkDegreesPerSecondCubed
			: bNormalized
				? Constraint->MaxNormalizedJerkPerSecondCubed
				: Constraint->MaxPositionJerkCentimetersPerSecondCubed;

		const FLLMNPCKinematicPeak Speed =
			FindDerivativePeak(DerivativeSamples, 1);
		const FLLMNPCKinematicPeak Acceleration =
			FindDerivativePeak(DerivativeSamples, 2);
		const FLLMNPCKinematicPeak Jerk =
			FindDerivativePeak(DerivativeSamples, 3);
		FLLMNPCKinematicTrackMetrics& Metrics =
			Report.TrackMetrics.AddDefaulted_GetRef();
		Metrics.ControlId = Track.ControlId;
		for (const FLLMNPCKinematicSample& Sample : Samples)
		{
			Metrics.MaxAbsoluteValue = FMath::Max(
				Metrics.MaxAbsoluteValue,
				FMath::Abs(Sample.Value)
			);
		}
		Metrics.MaxAbsoluteSpeed = Speed.Value;
		Metrics.MaxAbsoluteAcceleration = Acceleration.Value;
		Metrics.MaxAbsoluteJerk = Jerk.Value;
		if (!Samples.IsEmpty())
		{
			Metrics.StartAbsoluteValue = FMath::Abs(Samples[0].Value);
			Metrics.EndAbsoluteValue = FMath::Abs(Samples.Last().Value);
		}
		if (SpeedLimit > 0.0 && Speed.Value > SpeedLimit)
		{
			AddKinematicIssue(
				Report,
				TEXT("LLMNPC_KINEMATIC_SPEED_LIMIT"),
				ELLMNPCKinematicIssueSeverity::Error,
				FieldPath,
				Speed.Time,
				Speed.Value,
				SpeedLimit,
				TEXT("Sampled speed exceeds the Skeleton Profile limit.")
			);
		}
		if (AccelerationLimit > 0.0 && Acceleration.Value > AccelerationLimit)
		{
			AddKinematicIssue(
				Report,
				TEXT("LLMNPC_KINEMATIC_ACCELERATION_LIMIT"),
				ELLMNPCKinematicIssueSeverity::Error,
				FieldPath,
				Acceleration.Time,
				Acceleration.Value,
				AccelerationLimit,
				TEXT("Sampled acceleration exceeds the Skeleton Profile limit.")
			);
		}
		if (JerkLimit > 0.0 && Jerk.Value > JerkLimit)
		{
			AddKinematicIssue(
				Report,
				TEXT("LLMNPC_KINEMATIC_JERK_LIMIT"),
				ELLMNPCKinematicIssueSeverity::Error,
				FieldPath,
				Jerk.Time,
				Jerk.Value,
				JerkLimit,
				TEXT("Sampled jerk exceeds the Skeleton Profile limit.")
			);
		}

		if (
			Track.TrackType != ELLMMotionTrackType::Hold &&
			!Samples.IsEmpty()
		)
		{
			const double StartAbsolute = FMath::Abs(Samples[0].Value);
			const double EndAbsolute = FMath::Abs(Samples.Last().Value);
			const double WorstEnd = FMath::Max(StartAbsolute, EndAbsolute);
			if (WorstEnd > Settings.EndPoseTolerance)
			{
				AddKinematicIssue(
					Report,
					TEXT("LLMNPC_KINEMATIC_END_POSE_ERROR"),
					ELLMNPCKinematicIssueSeverity::Error,
					FieldPath,
					EndAbsolute >= StartAbsolute ? Plan.Clip.Duration : 0.0f,
					WorstEnd,
					Settings.EndPoseTolerance,
					TEXT("The sampled track does not blend to the base pose at both boundaries.")
				);
			}
		}
	}

	const bool bHasError = Report.Issues.ContainsByPredicate(
		[](const FLLMNPCKinematicValidationIssue& Issue)
		{
			return Issue.Severity == ELLMNPCKinematicIssueSeverity::Error;
		}
	);
	Report.bPassed = !bHasError;
	Report.bBlockingFailure = Report.bBaselineApproved && bHasError;
	Report.ReportHash = BuildReportHash(Report);
	return Report;
}
