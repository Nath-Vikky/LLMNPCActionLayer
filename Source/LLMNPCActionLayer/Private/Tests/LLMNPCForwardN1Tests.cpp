#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"
#include "JsonObjectConverter.h"
#include "LLMNPCMotionComponent.h"
#include "LLMNPCMotionSampler.h"
#include "Quality/LLMNPCKinematicValidator.h"
#include "Quality/LLMNPCPoseOutputContract.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr uint32 ForwardN1TestFlags =
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::EngineFilter;

ULLMNPCSkeletonProfile* LoadMannyProfile()
{
	return LoadObject<ULLMNPCSkeletonProfile>(
		nullptr,
		TEXT("/LLMNPCActionLayer/LLMNPC/SkeletonProfiles/SP_UE5_Manny_v1.SP_UE5_Manny_v1")
	);
}

FLLMMotionPlan MakeSingleTrackPlan(
	FName ControlId,
	ELLMMotionTrackType TrackType,
	float Amplitude
)
{
	FLLMMotionPlan Plan;
	Plan.Version = TEXT("1.0");
	Plan.Intent = TEXT("forward_n1_test");
	Plan.Clip.ClipId = TEXT("forward_n1_test.clip");
	Plan.Clip.Duration = 1.0f;
	Plan.Clip.BlendIn = 0.0f;
	Plan.Clip.BlendOut = 0.0f;
	FLLMMotionTrack& Track = Plan.Clip.Tracks.AddDefaulted_GetRef();
	Track.ControlId = ControlId;
	Track.TrackType = TrackType;
	Track.StartTime = 0.0f;
	Track.EndTime = 1.0f;
	Track.Amplitude = Amplitude;
	return Plan;
}

FLLMNPCSemanticCapability* FindCapability(
	FLLMNPCSkeletonCapabilitySnapshot& Snapshot,
	FName CapabilityId
)
{
	return Snapshot.Capabilities.FindByPredicate(
		[CapabilityId](const FLLMNPCSemanticCapability& Capability)
		{
			return Capability.CapabilityId == CapabilityId;
		}
	);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN1MannyProfileContractTest,
	"LLMNPCActionLayer.ForwardN1.Profile.MannyCapabilityCoverage",
	ForwardN1TestFlags
)

bool FLLMNPCForwardN1MannyProfileContractTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* Profile = LoadMannyProfile();
	TestNotNull(TEXT("The shipped Manny Profile loads"), Profile);
	if (!Profile)
	{
		return false;
	}

	FString Error;
	TestTrue(TEXT("The N1 Manny Profile validates"), Profile->ValidateProfile(Error));
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	const FLLMNPCSkeletonProfileQualityReport Quality =
		Profile->BuildQualityReport();
	TestTrue(TEXT("The N1 Capability quality gate passes"), Quality.bCapabilityReady);
	TestTrue(TEXT("Both shoulders are mapped"), FMath::IsNearlyEqual(Quality.ShoulderCoverage, 1.0f));
	TestTrue(
		TEXT("Both shoulder axes are calibrated"),
		FMath::IsNearlyEqual(Quality.ShoulderAxisCalibrationCoverage, 1.0f)
	);
	TestTrue(
		TEXT("Relaxed and Curl poses cover all Manny fingers"),
		FMath::IsNearlyEqual(Quality.ExtendedFingerPoseCoverage, 1.0f)
	);
	TestTrue(
		TEXT("Required derivative constraints are present"),
		FMath::IsNearlyEqual(Quality.KinematicConstraintCoverage, 1.0f)
	);
	TestNotNull(
		TEXT("Relaxed fingers have normalized derivative constraints"),
		Profile->FindControlConstraint(TEXT("right_fingers.relaxed"))
	);
	TestTrue(
		TEXT("Required upper-body collision proxies are present"),
		FMath::IsNearlyEqual(Quality.CollisionProxyCoverage, 1.0f)
	);

	const FLLMNPCPoseBoneBindings Bindings = Profile->BuildPoseBoneBindings();
	TestEqual(TEXT("Right shoulder binding"), Bindings.RightShoulder, FName(TEXT("clavicle_r")));
	TestEqual(TEXT("Left shoulder binding"), Bindings.LeftShoulder, FName(TEXT("clavicle_l")));
	TestEqual(TEXT("Right hand has 15 Relaxed rotations"), Bindings.RightFingerRelaxedRotations.Num(), 15);
	TestEqual(TEXT("Left hand has 15 Curl rotations"), Bindings.LeftFingerCurlRotations.Num(), 15);
	TestEqual(
		TEXT("The shipped Manny finger calibration is current"),
		Profile->FingerPoseCalibrationRevision,
		2
	);
	TestTrue(
		TEXT("Right Relaxed opens the index finger relative to the base pose"),
		Bindings.RightFingerRelaxedRotations.IsValidIndex(4) &&
		Bindings.RightFingerRelaxedRotations[4].Yaw > 0.0f
	);
	TestTrue(
		TEXT("Right Curl remains a clearly closed index finger"),
		Bindings.RightFingerCurlRotations.IsValidIndex(4) &&
		Bindings.RightFingerCurlRotations[4].Yaw < -40.0f
	);
	TestTrue(
		TEXT("Relaxed and Curl remain visually distinct"),
		Bindings.RightFingerRelaxedRotations.IsValidIndex(4) &&
		Bindings.RightFingerCurlRotations.IsValidIndex(4) &&
		FMath::Abs(
			Bindings.RightFingerRelaxedRotations[4].Yaw -
			Bindings.RightFingerCurlRotations[4].Yaw
		) >= 50.0f
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN1CapabilityModelViewTest,
	"LLMNPCActionLayer.ForwardN1.Capability.DeterministicSafeModelView",
	ForwardN1TestFlags
)

bool FLLMNPCForwardN1CapabilityModelViewTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* Profile = LoadMannyProfile();
	TestNotNull(TEXT("The shipped Manny Profile loads"), Profile);
	if (!Profile)
	{
		return false;
	}

	FLLMNPCSkeletonCapabilitySnapshot First;
	FLLMNPCSkeletonCapabilitySnapshot Second;
	const FLLMNPCSkeletonCapabilityBuildResult FirstResult =
		FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
			*Profile,
			nullptr,
			FDateTime(2026, 7, 25, 1, 0, 0),
			First
		);
	const FLLMNPCSkeletonCapabilityBuildResult SecondResult =
		FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
			*Profile,
			nullptr,
			FDateTime(2026, 7, 25, 2, 0, 0),
			Second
		);
	TestTrue(TEXT("First Capability build succeeds"), FirstResult.bSucceeded);
	TestTrue(TEXT("Second Capability build succeeds"), SecondResult.bSucceeded);
	TestEqual(
		TEXT("GeneratedAt does not change the Capability Hash"),
		First.CapabilityHash,
		Second.CapabilityHash
	);
	TestNotEqual(
		TEXT("GeneratedAt remains useful artifact metadata"),
		First.GeneratedAt,
		Second.GeneratedAt
	);
	TestNotNull(TEXT("Nod is exposed"), FindCapability(First, TEXT("head.nod")));
	TestNotNull(TEXT("Shoulder Shrug is exposed"), FindCapability(First, TEXT("shoulder.shrug")));
	TestNotNull(TEXT("Relaxed fingers are exposed"), FindCapability(First, TEXT("hand.pose.relaxed")));
	TestNotNull(TEXT("Curl fingers are exposed"), FindCapability(First, TEXT("hand.pose.curl")));

	FString ModelJson;
	FString Error;
	TestTrue(
		TEXT("The model-safe view serializes"),
		FLLMNPCSkeletonCapabilityBuilder::BuildModelViewJson(
			First,
			ModelJson,
			Error
		)
	);
	if (!Error.IsEmpty())
	{
		AddError(Error);
	}
	for (const TCHAR* RestrictedValue : {
		TEXT("clavicle_r"),
		TEXT("upperarm_r"),
		TEXT("lowerarm_r"),
		TEXT("hand_r"),
		TEXT("thumb_01_r"),
		TEXT("spine_03"),
		TEXT("right_hand.ik"),
		TEXT("right_fingers.open"),
		TEXT("compact_pose_index"),
		TEXT("quaternion"),
		TEXT("component_space")
	})
	{
		TestFalse(
			FString::Printf(TEXT("Model view omits internal value '%s'"), RestrictedValue),
			ModelJson.Contains(RestrictedValue, ESearchCase::IgnoreCase)
		);
	}
	FString RestrictedField;
	TestFalse(
		TEXT("The recursive restricted-key audit passes"),
		FLLMNPCSkeletonCapabilityBuilder::ModelViewContainsRestrictedFields(
			ModelJson,
			RestrictedField
		)
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN1CapabilityInvalidationTest,
	"LLMNPCActionLayer.ForwardN1.Capability.ConstraintInvalidatesHash",
	ForwardN1TestFlags
)

bool FLLMNPCForwardN1CapabilityInvalidationTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* SourceProfile = LoadMannyProfile();
	TestNotNull(TEXT("The shipped Manny Profile loads"), SourceProfile);
	if (!SourceProfile)
	{
		return false;
	}

	ULLMNPCSkeletonProfile* ChangedProfile =
		DuplicateObject<ULLMNPCSkeletonProfile>(SourceProfile, GetTransientPackage());
	TestNotNull(TEXT("A transient Profile copy can be created"), ChangedProfile);
	if (!ChangedProfile)
	{
		return false;
	}
	FLLMNPCSkeletonCapabilitySnapshot Before;
	FLLMNPCSkeletonCapabilitySnapshot After;
	const FDateTime FixedTime(2026, 7, 25, 1, 0, 0);
	TestTrue(
		TEXT("Baseline Capability builds"),
		FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
			*ChangedProfile,
			nullptr,
			FixedTime,
			Before
		).bSucceeded
	);
	ULLMNPCSkeletonProfile* ApprovalMetadataProfile =
		DuplicateObject<ULLMNPCSkeletonProfile>(
			SourceProfile,
			GetTransientPackage()
		);
	TestNotNull(
		TEXT("An approval-metadata Profile copy can be created"),
		ApprovalMetadataProfile
	);
	if (!ApprovalMetadataProfile)
	{
		return false;
	}
	ApprovalMetadataProfile->UpperBodyConstraints.bKinematicBaselineApproved =
		!SourceProfile->UpperBodyConstraints.bKinematicBaselineApproved;
	ApprovalMetadataProfile->UpperBodyConstraints.ValidationBaselineVersion =
		TEXT("metadata_only_change");
	ApprovalMetadataProfile->UpperBodyConstraints.ValidationBaselineHash =
		TEXT("md5:metadata_only_change");
	FLLMNPCSkeletonCapabilitySnapshot MetadataOnly;
	TestTrue(
		TEXT("Approval-metadata Capability builds"),
		FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
			*ApprovalMetadataProfile,
			nullptr,
			FixedTime,
			MetadataOnly
		).bSucceeded
	);
	TestEqual(
		TEXT("Human approval metadata does not invalidate behavior Capability"),
		Before.CapabilityHash,
		MetadataOnly.CapabilityHash
	);
	FLLMNPCKinematicControlConstraint* Constraint =
		ChangedProfile->ControlConstraints.FindByPredicate(
			[](const FLLMNPCKinematicControlConstraint& Candidate)
			{
				return Candidate.ControlId == TEXT("head.pitch");
			}
		);
	TestNotNull(TEXT("Head Pitch has a calibrated constraint"), Constraint);
	if (!Constraint)
	{
		return false;
	}
	Constraint->MaxAngularSpeedDegreesPerSecond += 1.0f;
	TestTrue(
		TEXT("Changed Capability builds"),
		FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
			*ChangedProfile,
			nullptr,
			FixedTime,
			After
		).bSucceeded
	);
	TestNotEqual(
		TEXT("A behavior-affecting constraint changes the Capability Hash"),
		Before.CapabilityHash,
		After.CapabilityHash
	);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN1PoseSamplingTest,
	"LLMNPCActionLayer.ForwardN1.Pose.ShoulderAndExtendedFingers",
	ForwardN1TestFlags
)

bool FLLMNPCForwardN1PoseSamplingTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	FLLMMotionClip Clip;
	Clip.Duration = 1.0f;
	Clip.BlendIn = 0.1f;
	Clip.BlendOut = 0.1f;

	FLLMMotionTrack& Shoulder = Clip.Tracks.AddDefaulted_GetRef();
	Shoulder.ControlId = TEXT("right_shoulder.pitch");
	Shoulder.TrackType = ELLMMotionTrackType::Hold;
	Shoulder.StartTime = 0.0f;
	Shoulder.EndTime = 1.0f;
	Shoulder.Amplitude = 12.0f;

	FLLMMotionTrack& Relaxed = Clip.Tracks.AddDefaulted_GetRef();
	Relaxed.ControlId = TEXT("right_fingers.relaxed");
	Relaxed.TrackType = ELLMMotionTrackType::Hold;
	Relaxed.StartTime = 0.0f;
	Relaxed.EndTime = 1.0f;
	Relaxed.Amplitude = 0.65f;

	FLLMMotionTrack& Curl = Clip.Tracks.AddDefaulted_GetRef();
	Curl.ControlId = TEXT("left_fingers.curl");
	Curl.TrackType = ELLMMotionTrackType::Hold;
	Curl.StartTime = 0.0f;
	Curl.EndTime = 1.0f;
	Curl.Amplitude = 0.8f;

	FLLMProceduralPoseSnapshot Snapshot;
	const TMap<FString, TObjectPtr<AActor>> EmptyTargets;
	FLLMNPCMotionSampler::SampleClip(
		Clip,
		nullptr,
		nullptr,
		EmptyTargets,
		0.5f,
		Snapshot
	);
	TestEqual(TEXT("Shoulder control reaches the Snapshot"), Snapshot.RightShoulderAdditiveRotation.Pitch, 12.0);
	TestEqual(TEXT("Relaxed pose reaches the Snapshot"), Snapshot.RightFingersRelaxed, 0.65f);
	TestEqual(TEXT("Curl pose reaches the Snapshot"), Snapshot.LeftFingersCurl, 0.8f);

	Snapshot = FLLMProceduralPoseSnapshot();
	TestTrue(TEXT("Snapshot reset clears shoulder rotation"), Snapshot.RightShoulderAdditiveRotation.IsNearlyZero());
	TestEqual(TEXT("Snapshot reset clears Relaxed pose"), Snapshot.RightFingersRelaxed, 0.0f);
	TestEqual(TEXT("Snapshot reset clears Curl pose"), Snapshot.LeftFingersCurl, 0.0f);

	const ULLMNPCMotionComponent* MotionComponent =
		NewObject<ULLMNPCMotionComponent>();
	for (const ELLMNPCMotionDebugSample ReviewSample : {
		ELLMNPCMotionDebugSample::ForwardN1ShoulderShrug,
		ELLMNPCMotionDebugSample::ForwardN1HandRelaxed,
		ELLMNPCMotionDebugSample::ForwardN1HandCurl
	})
	{
		FLLMMotionPlan ReviewPlan;
		const FString ReviewJson =
			MotionComponent->BuildSampleMotionPlanJson(
				ReviewSample,
				FString()
			);
		TestTrue(
			TEXT("Each Forward N1 visual review preset serializes"),
			FJsonObjectConverter::JsonObjectStringToUStruct(
				ReviewJson,
				&ReviewPlan,
				0,
				0
			)
		);
		TestTrue(
			TEXT("Each Forward N1 visual review preset has tracks"),
			!ReviewPlan.Clip.Tracks.IsEmpty()
		);
		if (ReviewSample == ELLMNPCMotionDebugSample::ForwardN1ShoulderShrug)
		{
			const FLLMMotionTrack* RightShoulder =
				ReviewPlan.Clip.Tracks.FindByPredicate(
					[](const FLLMMotionTrack& Track)
					{
						return Track.ControlId == TEXT("right_shoulder.roll");
					}
				);
			TestNotNull(
				TEXT("Shoulder review drives the right shoulder"),
				RightShoulder
			);
			TestTrue(
				TEXT("Shoulder review has a clearly visible calibrated amplitude"),
				RightShoulder && FMath::Abs(RightShoulder->Amplitude) >= 20.0f
			);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN1PoseOutputContractTest,
	"LLMNPCActionLayer.ForwardN1.Pose.OutputOrderingAndThreadSafety",
	ForwardN1TestFlags
)

bool FLLMNPCForwardN1PoseOutputContractTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	TArray<FBoneTransform> BoneTransforms = {
		FBoneTransform(FCompactPoseBoneIndex(7), FTransform(FVector(7.0f, 0.0f, 0.0f))),
		FBoneTransform(FCompactPoseBoneIndex(2), FTransform(FVector(2.0f, 0.0f, 0.0f))),
		FBoneTransform(FCompactPoseBoneIndex(7), FTransform(FVector(8.0f, 0.0f, 0.0f))),
		FBoneTransform(FCompactPoseBoneIndex(INDEX_NONE), FTransform::Identity)
	};
	TestFalse(
		TEXT("The deliberately malformed buffer starts invalid"),
		FLLMNPCPoseOutputContract::IsValidFinalBuffer(BoneTransforms)
	);
	FLLMNPCPoseOutputContract::FinalizeBoneTransforms(BoneTransforms);
	TestEqual(TEXT("Invalid and duplicate transforms are removed"), BoneTransforms.Num(), 2);
	TestTrue(
		TEXT("Finalized transforms are unique and sorted"),
		FLLMNPCPoseOutputContract::IsValidFinalBuffer(BoneTransforms)
	);
	TestEqual(TEXT("First Compact Pose index is sorted"), BoneTransforms[0].BoneIndex.GetInt(), 2);
	TestEqual(TEXT("Second Compact Pose index is sorted"), BoneTransforms[1].BoneIndex.GetInt(), 7);

	for (TFieldIterator<FProperty> PropertyIt(FLLMProceduralPoseSnapshot::StaticStruct()); PropertyIt; ++PropertyIt)
	{
		TestNull(
			FString::Printf(
				TEXT("Anim-thread Snapshot field '%s' is not a UObject reference"),
				*PropertyIt->GetName()
			),
			CastField<FObjectPropertyBase>(*PropertyIt)
		);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLLMNPCForwardN1KinematicValidatorTest,
	"LLMNPCActionLayer.ForwardN1.Quality.KinematicLimits",
	ForwardN1TestFlags
)

bool FLLMNPCForwardN1KinematicValidatorTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);
	const ULLMNPCSkeletonProfile* SourceProfile = LoadMannyProfile();
	TestNotNull(TEXT("The shipped Manny Profile loads"), SourceProfile);
	if (!SourceProfile)
	{
		return false;
	}
	ULLMNPCSkeletonProfile* Profile =
		DuplicateObject<ULLMNPCSkeletonProfile>(SourceProfile, GetTransientPackage());
	Profile->UpperBodyConstraints.bKinematicBaselineApproved = true;

	FLLMMotionPlan JointLimitPlan = MakeSingleTrackPlan(
		TEXT("head.pitch"),
		ELLMMotionTrackType::Hold,
		1000.0f
	);
	const FLLMNPCKinematicQualityReport JointLimitReport =
		FLLMNPCKinematicValidator::ValidatePlan(
			JointLimitPlan,
			*Profile,
			nullptr,
			TEXT("md5:test")
		);
	TestTrue(
		TEXT("Joint overrun is detected"),
		JointLimitReport.Issues.ContainsByPredicate(
			[](const FLLMNPCKinematicValidationIssue& Issue)
			{
				return Issue.Code == TEXT("LLMNPC_KINEMATIC_JOINT_LIMIT");
			})
	);
	TestTrue(TEXT("Approved baseline makes a quality error blocking"), JointLimitReport.bBlockingFailure);

	FLLMMotionPlan SpeedPlan = MakeSingleTrackPlan(
		TEXT("head.pitch"),
		ELLMMotionTrackType::Keyframes,
		0.0f
	);
	SpeedPlan.Clip.Tracks[0].FloatKeys = {
		{0.0f, 0.0f},
		{0.01f, 25.0f},
		{1.0f, 0.0f}
	};
	FLLMNPCKinematicControlConstraint* HeadConstraint =
		Profile->ControlConstraints.FindByPredicate(
			[](const FLLMNPCKinematicControlConstraint& Candidate)
			{
				return Candidate.ControlId == TEXT("head.pitch");
			});
	TestNotNull(TEXT("Head Pitch constraint exists"), HeadConstraint);
	if (!HeadConstraint)
	{
		return false;
	}
	HeadConstraint->MaxAngularSpeedDegreesPerSecond = 10.0f;
	const FLLMNPCKinematicQualityReport SpeedReport =
		FLLMNPCKinematicValidator::ValidatePlan(
			SpeedPlan,
			*Profile,
			nullptr,
			TEXT("md5:test")
		);
	TestTrue(
		TEXT("Abrupt motion is detected"),
		SpeedReport.Issues.ContainsByPredicate(
			[](const FLLMNPCKinematicValidationIssue& Issue)
			{
				return Issue.Code == TEXT("LLMNPC_KINEMATIC_SPEED_LIMIT");
			})
	);

	FLLMMotionPlan IrregularKeyPlan = MakeSingleTrackPlan(
		TEXT("head.pitch"),
		ELLMMotionTrackType::Keyframes,
		0.0f
	);
	IrregularKeyPlan.Clip.Tracks[0].FloatKeys = {
		{0.0f, 0.0f},
		{0.3333f, 8.0f},
		{1.0f, 20.0f}
	};
	HeadConstraint->MaxAngularSpeedDegreesPerSecond = 10000.0f;
	HeadConstraint->MaxAngularAccelerationDegreesPerSecondSquared = 100000.0f;
	HeadConstraint->MaxAngularJerkDegreesPerSecondCubed = 1000000.0f;
	const FLLMNPCKinematicQualityReport IrregularKeyReport =
		FLLMNPCKinematicValidator::ValidatePlan(
			IrregularKeyPlan,
			*Profile,
			nullptr,
			TEXT("md5:test")
		);
	const FLLMNPCKinematicTrackMetrics* IrregularKeyMetrics =
		IrregularKeyReport.TrackMetrics.FindByPredicate(
			[](const FLLMNPCKinematicTrackMetrics& Metrics)
			{
				return Metrics.ControlId == TEXT("head.pitch");
			}
		);
	TestNotNull(
		TEXT("The irregular-key track emits derivative metrics"),
		IrregularKeyMetrics
	);
	if (IrregularKeyMetrics)
	{
		TestTrue(
			TEXT("Exact key samples do not create artificial acceleration spikes"),
			IrregularKeyMetrics->MaxAbsoluteAcceleration < 2000.0
		);
		TestTrue(
			TEXT("Exact key samples do not create artificial jerk spikes"),
			IrregularKeyMetrics->MaxAbsoluteJerk < 200000.0
		);
	}

	FLLMMotionPlan ReachPlan = MakeSingleTrackPlan(
		TEXT("right_hand.ik"),
		ELLMMotionTrackType::IKReach,
		0.0f
	);
	ReachPlan.Clip.Tracks[0].Reach = 1.0f;
	const FLLMNPCKinematicQualityReport ReachReport =
		FLLMNPCKinematicValidator::ValidatePlan(
			ReachPlan,
			*Profile,
			nullptr,
			TEXT("md5:test")
		);
	TestTrue(
		TEXT("IK overreach is detected"),
		ReachReport.Issues.ContainsByPredicate(
			[](const FLLMNPCKinematicValidationIssue& Issue)
			{
				return Issue.Code == TEXT("LLMNPC_KINEMATIC_IK_OVERREACH");
			})
	);

	Profile->UpperBodyConstraints.bKinematicBaselineApproved = false;
	const FLLMNPCKinematicQualityReport DiagnosticReport =
		FLLMNPCKinematicValidator::ValidatePlan(
			JointLimitPlan,
			*Profile,
			nullptr,
			TEXT("md5:test")
		);
	TestFalse(
		TEXT("An unapproved baseline reports findings without blocking publication"),
		DiagnosticReport.bBlockingFailure
	);
	return true;
}

#endif
