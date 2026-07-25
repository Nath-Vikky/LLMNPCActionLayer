#include "Capabilities/LLMNPCSkeletonCapabilityBuilder.h"

#include "Dom/JsonObject.h"
#include "LLMNPCControlManifest.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Skeleton/LLMNPCSkeletonProfile.h"

namespace
{
constexpr const TCHAR* CapabilitySchemaVersion = TEXT("llmnpc.skeleton_capability.v1");

FLLMNPCCapabilityParameterRange MakeRange(
	FName ParameterId,
	FName ValueType,
	FName Unit,
	double MinValue,
	double MaxValue
)
{
	FLLMNPCCapabilityParameterRange Range;
	Range.ParameterId = ParameterId;
	Range.ValueType = ValueType;
	Range.Unit = Unit;
	Range.MinValue = MinValue;
	Range.MaxValue = MaxValue;
	return Range;
}

FLLMNPCCapabilityParameterRange NormalizedRange(
	FName ParameterId,
	double MinValue = 0.0,
	double MaxValue = 1.0
)
{
	return MakeRange(
		ParameterId,
		TEXT("normalized_float"),
		TEXT("normalized"),
		MinValue,
		MaxValue
	);
}

FLLMNPCSemanticCapability MakeCapability(
	FName CapabilityId,
	FName BodyRegion,
	const TCHAR* Description
)
{
	FLLMNPCSemanticCapability Capability;
	Capability.CapabilityId = CapabilityId;
	Capability.BodyRegion = BodyRegion;
	Capability.Description = Description;
	Capability.bRuntimeRecipeAllowed = true;
	return Capability;
}

bool HasSemantic(const ULLMNPCSkeletonProfile& Profile, FName Semantic)
{
	return !Profile.FindBoneName(Semantic).IsNone();
}

bool HasFingerPose(const ULLMNPCSkeletonProfile& Profile, FName PoseId)
{
	return Profile.FingerPoses.ContainsByPredicate(
		[PoseId](const FLLMNPCFingerPoseProfile& Pose)
		{
			return
				Pose.PoseId == PoseId ||
				Pose.PoseId == FName(*FString::Printf(TEXT("fingers_%s"), *PoseId.ToString()));
		}
	);
}

const FLLMControlDefinition* FindControl(
	const ULLMNPCControlManifest* Manifest,
	FName ControlId
)
{
	return Manifest
		? Manifest->FindControl(ControlId)
		: ULLMNPCControlManifest::FindBuiltInControl(ControlId);
}

bool HasControls(
	const ULLMNPCControlManifest* Manifest,
	std::initializer_list<FName> ControlIds
)
{
	for (const FName ControlId : ControlIds)
	{
		if (!FindControl(Manifest, ControlId))
		{
			return false;
		}
	}
	return true;
}

void AddCommonMotionRanges(FLLMNPCSemanticCapability& Capability)
{
	Capability.ParameterRanges = {
		NormalizedRange(TEXT("amplitude"), 0.2, 1.0),
		MakeRange(TEXT("speed"), TEXT("speed_multiplier"), TEXT("multiplier"), 0.6, 1.4),
		MakeRange(TEXT("duration"), TEXT("seconds"), TEXT("seconds"), 0.2, 4.0)
	};
}

void AddCycleRange(FLLMNPCSemanticCapability& Capability, int32 MinCycles, int32 MaxCycles)
{
	Capability.ParameterRanges.Add(
		MakeRange(TEXT("cycles"), TEXT("integer"), TEXT("count"), MinCycles, MaxCycles)
	);
}

void SortNames(TArray<FName>& Names)
{
	Names.Sort(
		[](const FName& A, const FName& B)
		{
			return A.LexicalLess(B);
		}
	);
}

void NormalizeCapability(FLLMNPCSemanticCapability& Capability)
{
	SortNames(Capability.SupportedSides);
	SortNames(Capability.Requires);
	SortNames(Capability.ConflictsWith);
	SortNames(Capability.TargetModes);
	SortNames(Capability.InternalControlIds);
	Capability.ParameterRanges.Sort(
		[](const FLLMNPCCapabilityParameterRange& A, const FLLMNPCCapabilityParameterRange& B)
		{
			return A.ParameterId.LexicalLess(B.ParameterId);
		}
	);
}

void AddHeadCapabilities(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* Manifest,
	TArray<FLLMNPCSemanticCapability>& OutCapabilities
)
{
	if (!HasSemantic(Profile, TEXT("head")))
	{
		return;
	}

	struct FHeadCapabilityDefinition
	{
		FName CapabilityId;
		FName ControlId;
		const TCHAR* Description;
	};
	const FHeadCapabilityDefinition Definitions[] = {
		{
			TEXT("head.nod"),
			TEXT("head.pitch"),
			TEXT("Move the head vertically to acknowledge, agree, or encourage.")
		},
		{
			TEXT("head.shake"),
			TEXT("head.yaw"),
			TEXT("Move the head laterally to disagree, refuse, or express uncertainty.")
		},
		{
			TEXT("head.tilt"),
			TEXT("head.roll"),
			TEXT("Tilt the head to show curiosity, sympathy, or uncertainty.")
		}
	};

	for (const FHeadCapabilityDefinition& Definition : Definitions)
	{
		if (!FindControl(Manifest, Definition.ControlId))
		{
			continue;
		}
		FLLMNPCSemanticCapability Capability = MakeCapability(
			Definition.CapabilityId,
			TEXT("head"),
			Definition.Description
		);
		Capability.SupportedSides = {TEXT("center")};
		Capability.TargetModes = {TEXT("none")};
		Capability.InternalControlIds = {Definition.ControlId};
		AddCommonMotionRanges(Capability);
		AddCycleRange(Capability, 1, 4);
		OutCapabilities.Add(MoveTemp(Capability));
	}

	if (FindControl(Manifest, TEXT("gaze.target")))
	{
		FLLMNPCSemanticCapability Gaze = MakeCapability(
			TEXT("gaze.track"),
			TEXT("gaze"),
			TEXT("Track a validated scene target with constrained head and gaze motion.")
		);
		Gaze.SupportedSides = {TEXT("center")};
		Gaze.TargetModes = {TEXT("scene_target")};
		Gaze.InternalControlIds = {TEXT("gaze.target")};
		Gaze.ParameterRanges = {
			NormalizedRange(TEXT("engagement"), 0.0, 1.0),
			MakeRange(TEXT("duration"), TEXT("seconds"), TEXT("seconds"), 0.2, 4.0)
		};
		OutCapabilities.Add(MoveTemp(Gaze));
	}
}

void AddChestCapabilities(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* Manifest,
	TArray<FLLMNPCSemanticCapability>& OutCapabilities
)
{
	if (!HasSemantic(Profile, TEXT("chest")))
	{
		return;
	}

	if (FindControl(Manifest, TEXT("chest.pitch")))
	{
		FLLMNPCSemanticCapability Lean = MakeCapability(
			TEXT("chest.lean"),
			TEXT("chest"),
			TEXT("Apply a small upper-body lean without moving the pelvis or feet.")
		);
		Lean.SupportedSides = {TEXT("center")};
		Lean.TargetModes = {TEXT("none"), TEXT("scene_target")};
		Lean.InternalControlIds = {TEXT("chest.pitch")};
		AddCommonMotionRanges(Lean);
		OutCapabilities.Add(MoveTemp(Lean));
	}

	if (FindControl(Manifest, TEXT("chest.yaw")))
	{
		FLLMNPCSemanticCapability Turn = MakeCapability(
			TEXT("chest.turn"),
			TEXT("chest"),
			TEXT("Apply a small upper-body turn while locomotion retains lower-body ownership.")
		);
		Turn.SupportedSides = {TEXT("left"), TEXT("right")};
		Turn.TargetModes = {TEXT("none"), TEXT("scene_target")};
		Turn.InternalControlIds = {TEXT("chest.yaw")};
		AddCommonMotionRanges(Turn);
		OutCapabilities.Add(MoveTemp(Turn));
	}
}

void AddShoulderCapability(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* Manifest,
	TArray<FLLMNPCSemanticCapability>& OutCapabilities
)
{
	FLLMNPCSemanticCapability Shrug = MakeCapability(
		TEXT("shoulder.shrug"),
		TEXT("shoulders"),
		TEXT("Raise one or both shoulders to express uncertainty, indifference, or lack of knowledge.")
	);
	if (
		HasSemantic(Profile, TEXT("shoulder_left")) &&
		HasControls(Manifest, {TEXT("left_shoulder.pitch")})
	)
	{
		Shrug.SupportedSides.Add(TEXT("left"));
		Shrug.InternalControlIds.Add(TEXT("left_shoulder.pitch"));
	}
	if (
		HasSemantic(Profile, TEXT("shoulder_right")) &&
		HasControls(Manifest, {TEXT("right_shoulder.pitch")})
	)
	{
		Shrug.SupportedSides.Add(TEXT("right"));
		Shrug.InternalControlIds.Add(TEXT("right_shoulder.pitch"));
	}
	if (Shrug.SupportedSides.Num() == 2)
	{
		Shrug.SupportedSides.Add(TEXT("both"));
	}
	if (Shrug.SupportedSides.IsEmpty())
	{
		return;
	}
	Shrug.TargetModes = {TEXT("none")};
	AddCommonMotionRanges(Shrug);
	OutCapabilities.Add(MoveTemp(Shrug));
}

void AddArmCapabilities(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* Manifest,
	TArray<FLLMNPCSemanticCapability>& OutCapabilities
)
{
	FLLMNPCSemanticCapability Reach = MakeCapability(
		TEXT("arm.reach"),
		TEXT("arms"),
		TEXT("Move one hand toward a validated target within the configured reach envelope.")
	);
	FLLMNPCSemanticCapability Present = MakeCapability(
		TEXT("arm.present"),
		TEXT("arms"),
		TEXT("Extend one arm with an open palm to present a person, object, or direction.")
	);
	FLLMNPCSemanticCapability Wave = MakeCapability(
		TEXT("hand.wave_arc"),
		TEXT("hands"),
		TEXT("Raise one hand and move it laterally in a greeting or farewell arc.")
	);

	auto AddSide = [&](FName Side, const TCHAR* SemanticSuffix)
	{
		const FString Suffix(SemanticSuffix);
		const bool bHasArm =
			HasSemantic(Profile, FName(*FString::Printf(TEXT("upperarm_%s"), *Suffix))) &&
			HasSemantic(Profile, FName(*FString::Printf(TEXT("lowerarm_%s"), *Suffix))) &&
			HasSemantic(Profile, FName(*FString::Printf(TEXT("hand_%s"), *Suffix)));
		const FName IKControl(*FString::Printf(TEXT("%s_hand.ik"), *Suffix));
		const FName OffsetX(*FString::Printf(TEXT("%s_hand.local_offset.x"), *Suffix));
		if (!bHasArm || !HasControls(Manifest, {IKControl, OffsetX}))
		{
			return;
		}

		Reach.SupportedSides.Add(Side);
		Reach.InternalControlIds.Append({IKControl, OffsetX});
		Present.SupportedSides.Add(Side);
		Present.InternalControlIds.Append({IKControl, OffsetX});
		Wave.SupportedSides.Add(Side);
		Wave.InternalControlIds.Append({IKControl, OffsetX});
	};

	AddSide(TEXT("left"), TEXT("left"));
	AddSide(TEXT("right"), TEXT("right"));
	if (!Reach.SupportedSides.IsEmpty())
	{
		Reach.TargetModes = {TEXT("scene_target"), TEXT("direction")};
		Reach.ConflictsWith = {
			TEXT("left_hand_busy"),
			TEXT("right_hand_busy"),
			TEXT("two_hand_interaction")
		};
		Reach.ParameterRanges = {
			NormalizedRange(TEXT("reach"), 0.05, 0.98),
			NormalizedRange(TEXT("height"), 0.0, 1.0),
			MakeRange(TEXT("duration"), TEXT("seconds"), TEXT("seconds"), 0.2, 4.0)
		};
		OutCapabilities.Add(MoveTemp(Reach));

		Present.TargetModes = {TEXT("scene_target"), TEXT("direction")};
		Present.Requires = {TEXT("hand.pose.open")};
		Present.ParameterRanges = {
			NormalizedRange(TEXT("amplitude"), 0.2, 1.0),
			NormalizedRange(TEXT("height"), 0.0, 1.0),
			MakeRange(TEXT("duration"), TEXT("seconds"), TEXT("seconds"), 0.2, 4.0)
		};
		OutCapabilities.Add(MoveTemp(Present));

		Wave.TargetModes = {TEXT("none"), TEXT("scene_target")};
		Wave.Requires = {TEXT("arm.reach"), TEXT("hand.pose.open")};
		Wave.ConflictsWith = {
			TEXT("left_hand_busy"),
			TEXT("right_hand_busy"),
			TEXT("two_hand_interaction")
		};
		Wave.ParameterRanges = {
			NormalizedRange(TEXT("amplitude"), 0.2, 1.0),
			NormalizedRange(TEXT("height"), 0.25, 0.9),
			MakeRange(TEXT("speed"), TEXT("speed_multiplier"), TEXT("multiplier"), 0.6, 1.4),
			MakeRange(TEXT("cycles"), TEXT("integer"), TEXT("count"), 1.0, 4.0)
		};
		OutCapabilities.Add(MoveTemp(Wave));
	}
}

void AddFingerCapability(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* Manifest,
	FName PoseId,
	FName CapabilityId,
	const TCHAR* Description,
	TArray<FLLMNPCSemanticCapability>& OutCapabilities
)
{
	if (!HasFingerPose(Profile, PoseId))
	{
		return;
	}

	FLLMNPCSemanticCapability Capability = MakeCapability(
		CapabilityId,
		TEXT("hands"),
		Description
	);
	for (const FName Side : {FName(TEXT("left")), FName(TEXT("right"))})
	{
		const FName ControlId(*FString::Printf(
			TEXT("%s_fingers.%s"),
			*Side.ToString(),
			*PoseId.ToString()
		));
		if (FindControl(Manifest, ControlId))
		{
			Capability.SupportedSides.Add(Side);
			Capability.InternalControlIds.Add(ControlId);
		}
	}
	if (Capability.SupportedSides.IsEmpty())
	{
		return;
	}
	Capability.TargetModes = {TEXT("none")};
	Capability.ParameterRanges = {NormalizedRange(TEXT("weight"), 0.0, 1.0)};
	OutCapabilities.Add(MoveTemp(Capability));
}

void BuildSemanticCapabilities(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* Manifest,
	TArray<FLLMNPCSemanticCapability>& OutCapabilities
)
{
	AddHeadCapabilities(Profile, Manifest, OutCapabilities);
	AddChestCapabilities(Profile, Manifest, OutCapabilities);
	AddShoulderCapability(Profile, Manifest, OutCapabilities);
	AddFingerCapability(
		Profile,
		Manifest,
		TEXT("open"),
		TEXT("hand.pose.open"),
		TEXT("Blend the selected hand toward the calibrated open-palm pose."),
		OutCapabilities
	);
	AddFingerCapability(
		Profile,
		Manifest,
		TEXT("point"),
		TEXT("hand.pose.point"),
		TEXT("Blend the selected hand toward the calibrated pointing pose."),
		OutCapabilities
	);
	AddFingerCapability(
		Profile,
		Manifest,
		TEXT("relaxed"),
		TEXT("hand.pose.relaxed"),
		TEXT("Blend the selected hand toward a natural relaxed finger pose."),
		OutCapabilities
	);
	AddFingerCapability(
		Profile,
		Manifest,
		TEXT("curl"),
		TEXT("hand.pose.curl"),
		TEXT("Curl the selected fingers within the calibrated safe pose."),
		OutCapabilities
	);
	AddArmCapabilities(Profile, Manifest, OutCapabilities);

	FLLMNPCSemanticCapability Hold = MakeCapability(
		TEXT("hold"),
		TEXT("upper_body"),
		TEXT("Hold the current recipe phase for a bounded duration.")
	);
	Hold.SupportedSides = {TEXT("center")};
	Hold.TargetModes = {TEXT("none")};
	Hold.ParameterRanges = {
		MakeRange(TEXT("duration"), TEXT("seconds"), TEXT("seconds"), 0.05, 2.0)
	};
	OutCapabilities.Add(MoveTemp(Hold));

	for (FLLMNPCSemanticCapability& Capability : OutCapabilities)
	{
		NormalizeCapability(Capability);
	}
	OutCapabilities.Sort(
		[](const FLLMNPCSemanticCapability& A, const FLLMNPCSemanticCapability& B)
		{
			return A.CapabilityId.LexicalLess(B.CapabilityId);
		}
	);
}

bool IsAllowedParameterType(FName ValueType)
{
	return
		ValueType == TEXT("normalized_float") ||
		ValueType == TEXT("speed_multiplier") ||
		ValueType == TEXT("seconds") ||
		ValueType == TEXT("integer");
}

bool IsAllowedUnit(FName Unit)
{
	return
		Unit == TEXT("normalized") ||
		Unit == TEXT("multiplier") ||
		Unit == TEXT("seconds") ||
		Unit == TEXT("count");
}

void ValidateCapabilities(
	const TArray<FLLMNPCSemanticCapability>& Capabilities,
	FLLMNPCSkeletonCapabilityBuildResult& OutResult
)
{
	TSet<FName> CapabilityIds;
	for (const FLLMNPCSemanticCapability& Capability : Capabilities)
	{
		if (
			Capability.CapabilityId.IsNone() ||
			CapabilityIds.Contains(Capability.CapabilityId)
		)
		{
			OutResult.Errors.Add(TEXT("LLMNPC_CAPABILITY_ID_INVALID"));
			continue;
		}
		CapabilityIds.Add(Capability.CapabilityId);
		if (Capability.Description.TrimStartAndEnd().IsEmpty())
		{
			OutResult.Errors.Add(FString::Printf(
				TEXT("LLMNPC_CAPABILITY_DESCRIPTION_MISSING:%s"),
				*Capability.CapabilityId.ToString()
			));
		}
		for (const FLLMNPCCapabilityParameterRange& Range : Capability.ParameterRanges)
		{
			if (
				Range.ParameterId.IsNone() ||
				!IsAllowedParameterType(Range.ValueType) ||
				!IsAllowedUnit(Range.Unit) ||
				!FMath::IsFinite(Range.MinValue) ||
				!FMath::IsFinite(Range.MaxValue) ||
				Range.MinValue > Range.MaxValue
			)
			{
				OutResult.Errors.Add(FString::Printf(
					TEXT("LLMNPC_CAPABILITY_PARAMETER_RANGE_INVALID:%s.%s"),
					*Capability.CapabilityId.ToString(),
					*Range.ParameterId.ToString()
				));
			}
		}
	}

	for (const FLLMNPCSemanticCapability& Capability : Capabilities)
	{
		for (const FName RequiredCapability : Capability.Requires)
		{
			if (!CapabilityIds.Contains(RequiredCapability))
			{
				OutResult.Warnings.Add(FString::Printf(
					TEXT("LLMNPC_CAPABILITY_REQUIREMENT_UNAVAILABLE:%s.%s"),
					*Capability.CapabilityId.ToString(),
					*RequiredCapability.ToString()
				));
			}
		}
	}
}

void AppendNameArray(FString& Canonical, const TCHAR* Label, const TArray<FName>& Names)
{
	TArray<FName> Sorted = Names;
	SortNames(Sorted);
	Canonical += Label;
	Canonical += TEXT("=");
	for (const FName Name : Sorted)
	{
		Canonical += Name.ToString();
		Canonical += TEXT(",");
	}
	Canonical += TEXT("\n");
}

void AppendVector(FString& Canonical, const FVector& Value)
{
	Canonical += FString::Printf(
		TEXT("%.9g,%.9g,%.9g"),
		Value.X,
		Value.Y,
		Value.Z
	);
}

void AppendRotator(FString& Canonical, const FRotator& Value)
{
	Canonical += FString::Printf(
		TEXT("%.9g,%.9g,%.9g"),
		Value.Pitch,
		Value.Yaw,
		Value.Roll
	);
}

FString BuildCanonicalHashInput(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* Manifest,
	const FLLMNPCSkeletonCapabilitySnapshot& Snapshot
)
{
	FString Canonical;
	Canonical += FString::Printf(
		TEXT("schema=%s\nprofile=%s\nprofile_version=%s\nskeleton=%s\nmanifest=%s\n"),
		CapabilitySchemaVersion,
		*Profile.ProfileId.ToString(),
		*Profile.SemanticVersion,
		*Profile.SkeletonSignature,
		*Snapshot.ControlManifestVersion
	);
	Canonical += TEXT("component_forward=");
	AppendVector(Canonical, Profile.ComponentForwardDirectionCS);
	Canonical += TEXT("\ncomponent_up=");
	AppendVector(Canonical, Profile.ComponentUpDirectionCS);
	Canonical += TEXT("\n");

	TArray<FName> SemanticKeys;
	Profile.SemanticBoneMap.GetKeys(SemanticKeys);
	SortNames(SemanticKeys);
	for (const FName Semantic : SemanticKeys)
	{
		Canonical += FString::Printf(
			TEXT("bone=%s:%s\n"),
			*Semantic.ToString(),
			*Profile.SemanticBoneMap.FindChecked(Semantic).ToString()
		);
	}

	TArray<FName> AxisKeys;
	Profile.AxisBases.GetKeys(AxisKeys);
	SortNames(AxisKeys);
	for (const FName Semantic : AxisKeys)
	{
		const FLLMNPCBoneAxisBasis& Basis = Profile.AxisBases.FindChecked(Semantic);
		Canonical += FString::Printf(TEXT("axis=%s:"), *Semantic.ToString());
		AppendVector(Canonical, Basis.PitchAxis);
		Canonical += TEXT("|");
		AppendVector(Canonical, Basis.YawAxis);
		Canonical += TEXT("|");
		AppendVector(Canonical, Basis.RollAxis);
		Canonical += TEXT("|");
		AppendRotator(Canonical, Basis.MinAdditiveRotation);
		Canonical += TEXT("|");
		AppendRotator(Canonical, Basis.MaxAdditiveRotation);
		Canonical += TEXT("\n");
	}

	TArray<FLLMNPCIKChainProfile> IKChains = Profile.IKChains;
	IKChains.Sort(
		[](const FLLMNPCIKChainProfile& A, const FLLMNPCIKChainProfile& B)
		{
			return A.ChainId.LexicalLess(B.ChainId);
		}
	);
	for (const FLLMNPCIKChainProfile& Chain : IKChains)
	{
		Canonical += FString::Printf(
			TEXT("ik=%s:%s:%s:%s:"),
			*Chain.ChainId.ToString(),
			*Chain.RootBoneSemantic.ToString(),
			*Chain.MidBoneSemantic.ToString(),
			*Chain.EndBoneSemantic.ToString()
		);
		AppendVector(Canonical, Chain.PoleDirectionCS);
		Canonical += FString::Printf(
			TEXT(":%.9g:%.9g:%.9g\n"),
			Chain.MinReachScale,
			Chain.MaxReachScale,
			Chain.PoleSafetyConeDegrees
		);
	}

	TArray<FLLMNPCFingerPoseProfile> FingerPoses = Profile.FingerPoses;
	FingerPoses.Sort(
		[](const FLLMNPCFingerPoseProfile& A, const FLLMNPCFingerPoseProfile& B)
		{
			return A.PoseId.LexicalLess(B.PoseId);
		}
	);
	for (const FLLMNPCFingerPoseProfile& Pose : FingerPoses)
	{
		TArray<FName> PoseBones;
		Pose.SemanticBoneRotations.GetKeys(PoseBones);
		SortNames(PoseBones);
		for (const FName Semantic : PoseBones)
		{
			Canonical += FString::Printf(
				TEXT("finger=%s:%s:"),
				*Pose.PoseId.ToString(),
				*Semantic.ToString()
			);
			AppendRotator(Canonical, Pose.SemanticBoneRotations.FindChecked(Semantic));
			Canonical += TEXT("\n");
		}
	}

	TArray<FLLMNPCKinematicControlConstraint> Constraints = Profile.ControlConstraints;
	Constraints.Sort(
		[](const FLLMNPCKinematicControlConstraint& A, const FLLMNPCKinematicControlConstraint& B)
		{
			return A.ControlId.LexicalLess(B.ControlId);
		}
	);
	for (const FLLMNPCKinematicControlConstraint& Constraint : Constraints)
	{
		Canonical += FString::Printf(
			TEXT("constraint=%s:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g:%.9g\n"),
			*Constraint.ControlId.ToString(),
			Constraint.MaxAngularSpeedDegreesPerSecond,
			Constraint.MaxAngularAccelerationDegreesPerSecondSquared,
			Constraint.MaxAngularJerkDegreesPerSecondCubed,
			Constraint.MaxPositionSpeedCentimetersPerSecond,
			Constraint.MaxPositionAccelerationCentimetersPerSecondSquared,
			Constraint.MaxPositionJerkCentimetersPerSecondCubed,
			Constraint.MaxNormalizedSpeedPerSecond,
			Constraint.MaxNormalizedAccelerationPerSecondSquared,
			Constraint.MaxNormalizedJerkPerSecondCubed
		);
	}

	TArray<FLLMNPCCollisionProxyProfile> Proxies = Profile.CollisionProxies;
	Proxies.Sort(
		[](const FLLMNPCCollisionProxyProfile& A, const FLLMNPCCollisionProxyProfile& B)
		{
			return A.ProxyId.LexicalLess(B.ProxyId);
		}
	);
	for (const FLLMNPCCollisionProxyProfile& Proxy : Proxies)
	{
		Canonical += FString::Printf(
			TEXT("proxy=%s:%d:%s:"),
			*Proxy.ProxyId.ToString(),
			static_cast<int32>(Proxy.Shape),
			*Proxy.AnchorBoneSemantic.ToString()
		);
		AppendVector(Canonical, Proxy.LocalOffset);
		Canonical += FString::Printf(
			TEXT(":%.9g:%.9g\n"),
			Proxy.RadiusCentimeters,
			Proxy.HalfHeightCentimeters
		);
	}

	const FLLMNPCUpperBodyConstraintProfile& Upper = Profile.UpperBodyConstraints;
	Canonical += FString::Printf(
		TEXT("upper=%.9g:%.9g:"),
		Upper.MaxHeadLookAngleDegrees,
		Upper.MaxChestAdditiveAngleDegrees
	);
	AppendVector(Canonical, Upper.HandReachBoundsMinCS);
	Canonical += TEXT(":");
	AppendVector(Canonical, Upper.HandReachBoundsMaxCS);
	Canonical += TEXT("\n");
	AppendNameArray(
		Canonical,
		TEXT("ground_contacts"),
		Profile.StableGroundContactBoneSemantics
	);

	TArray<FLLMControlDefinition> Controls = ULLMNPCControlManifest::GetBuiltInControls();
	if (Manifest)
	{
		for (const FLLMControlDefinition& Override : Manifest->Controls)
		{
			Controls.RemoveAll(
				[&Override](const FLLMControlDefinition& Existing)
				{
					return Existing.ControlId == Override.ControlId;
				}
			);
			Controls.Add(Override);
		}
	}
	Controls.Sort(
		[](const FLLMControlDefinition& A, const FLLMControlDefinition& B)
		{
			return A.ControlId.LexicalLess(B.ControlId);
		}
	);
	for (const FLLMControlDefinition& Control : Controls)
	{
		Canonical += FString::Printf(
			TEXT("control=%s:%d:%.9g:%.9g:%d:%d:%d\n"),
			*Control.ControlId.ToString(),
			static_cast<int32>(Control.SolverType),
			Control.MinValue,
			Control.MaxValue,
			Control.bRequiresTarget ? 1 : 0,
			Control.bAllowRuntimeModel ? 1 : 0,
			Control.bAllowTemplateAuthoring ? 1 : 0
		);
	}

	for (const FLLMNPCSemanticCapability& Capability : Snapshot.Capabilities)
	{
		Canonical += FString::Printf(
			TEXT("capability=%s:%s:%d:%d:%s\n"),
			*Capability.CapabilityId.ToString(),
			*Capability.BodyRegion.ToString(),
			Capability.bRuntimeRecipeAllowed ? 1 : 0,
			Capability.bAuthoringOnly ? 1 : 0,
			*Capability.Description
		);
		AppendNameArray(Canonical, TEXT("sides"), Capability.SupportedSides);
		AppendNameArray(Canonical, TEXT("requires"), Capability.Requires);
		AppendNameArray(Canonical, TEXT("conflicts"), Capability.ConflictsWith);
		AppendNameArray(Canonical, TEXT("targets"), Capability.TargetModes);
		AppendNameArray(Canonical, TEXT("internal_controls"), Capability.InternalControlIds);
		for (const FLLMNPCCapabilityParameterRange& Range : Capability.ParameterRanges)
		{
			Canonical += FString::Printf(
				TEXT("range=%s:%s:%s:%.17g:%.17g\n"),
				*Range.ParameterId.ToString(),
				*Range.ValueType.ToString(),
				*Range.Unit.ToString(),
				Range.MinValue,
				Range.MaxValue
			);
		}
	}

	Canonical += FString::Printf(
		TEXT("global=%.9g:%d:%d:%d\n"),
		Snapshot.GlobalLimits.MaxActionDurationSeconds,
		Snapshot.GlobalLimits.MaxPrimitiveCount,
		Snapshot.GlobalLimits.bAllowsLowerBody ? 1 : 0,
		Snapshot.GlobalLimits.bAllowsRootMotion ? 1 : 0
	);
	AppendNameArray(
		Canonical,
		TEXT("allowed_regions"),
		Snapshot.GlobalLimits.AllowedBodyRegions
	);
	return Canonical;
}

TArray<TSharedPtr<FJsonValue>> CapabilityNamesToJson(const TArray<FName>& Names)
{
	TArray<TSharedPtr<FJsonValue>> Values;
	Values.Reserve(Names.Num());
	for (const FName Name : Names)
	{
		Values.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	return Values;
}

TSharedRef<FJsonObject> CapabilityToModelJson(const FLLMNPCSemanticCapability& Capability)
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("capability_id"), Capability.CapabilityId.ToString());
	Object->SetStringField(TEXT("body_region"), Capability.BodyRegion.ToString());
	Object->SetArrayField(TEXT("supported_sides"), CapabilityNamesToJson(Capability.SupportedSides));

	TSharedRef<FJsonObject> ParameterRanges = MakeShared<FJsonObject>();
	for (const FLLMNPCCapabilityParameterRange& Range : Capability.ParameterRanges)
	{
		TSharedRef<FJsonObject> RangeObject = MakeShared<FJsonObject>();
		RangeObject->SetStringField(TEXT("type"), Range.ValueType.ToString());
		RangeObject->SetStringField(TEXT("unit"), Range.Unit.ToString());
		RangeObject->SetNumberField(TEXT("min"), Range.MinValue);
		RangeObject->SetNumberField(TEXT("max"), Range.MaxValue);
		ParameterRanges->SetObjectField(Range.ParameterId.ToString(), RangeObject);
	}
	Object->SetObjectField(TEXT("parameter_ranges"), ParameterRanges);
	Object->SetArrayField(TEXT("requires"), CapabilityNamesToJson(Capability.Requires));
	Object->SetArrayField(TEXT("conflicts_with"), CapabilityNamesToJson(Capability.ConflictsWith));
	Object->SetArrayField(TEXT("target_modes"), CapabilityNamesToJson(Capability.TargetModes));
	Object->SetBoolField(TEXT("runtime_recipe_allowed"), Capability.bRuntimeRecipeAllowed);
	Object->SetBoolField(TEXT("authoring_only"), Capability.bAuthoringOnly);
	Object->SetStringField(TEXT("description"), Capability.Description);
	return Object;
}
}

FLLMNPCSkeletonCapabilityBuildResult FLLMNPCSkeletonCapabilityBuilder::Build(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* ControlManifest,
	FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot
)
{
	return BuildAtTime(
		Profile,
		ControlManifest,
		FDateTime::UtcNow(),
		OutSnapshot
	);
}

FLLMNPCSkeletonCapabilityBuildResult FLLMNPCSkeletonCapabilityBuilder::BuildAtTime(
	const ULLMNPCSkeletonProfile& Profile,
	const ULLMNPCControlManifest* ControlManifest,
	const FDateTime& GeneratedAtUtc,
	FLLMNPCSkeletonCapabilitySnapshot& OutSnapshot
)
{
	OutSnapshot = FLLMNPCSkeletonCapabilitySnapshot();
	FLLMNPCSkeletonCapabilityBuildResult Result;

	FString ProfileError;
	if (!Profile.ValidateProfile(ProfileError))
	{
		Result.Errors.Add(ProfileError);
		return Result;
	}

	OutSnapshot.SchemaVersion = CapabilitySchemaVersion;
	OutSnapshot.ProfileId = Profile.ProfileId;
	OutSnapshot.ProfileSemanticVersion = Profile.SemanticVersion;
	OutSnapshot.SkeletonSignature = Profile.SkeletonSignature;
	OutSnapshot.ControlManifestVersion =
		ControlManifest && !ControlManifest->ManifestVersion.IsEmpty()
		? ControlManifest->ManifestVersion
		: ULLMNPCControlManifest::GetBuiltInManifestVersion();
	OutSnapshot.GeneratedAt = GeneratedAtUtc.ToIso8601();
	OutSnapshot.InternalPoseBindings = Profile.BuildPoseBoneBindings();
	OutSnapshot.InternalControlConstraints = Profile.ControlConstraints;
	OutSnapshot.InternalCollisionProxies = Profile.CollisionProxies;

	BuildSemanticCapabilities(Profile, ControlManifest, OutSnapshot.Capabilities);
	ValidateCapabilities(OutSnapshot.Capabilities, Result);

	const FLLMNPCSkeletonProfileQualityReport Quality = Profile.BuildQualityReport();
	if (!Quality.bCapabilityReady)
	{
		Result.Warnings.Add(TEXT("LLMNPC_CAPABILITY_PROFILE_QUALITY_INCOMPLETE"));
	}
	if (OutSnapshot.Capabilities.IsEmpty())
	{
		Result.Errors.Add(TEXT("LLMNPC_CAPABILITY_SET_EMPTY"));
	}

	if (Result.Errors.IsEmpty())
	{
		const FString Canonical = BuildCanonicalHashInput(
			Profile,
			ControlManifest,
			OutSnapshot
		);
		OutSnapshot.CapabilityHash = FString::Printf(
			TEXT("md5:%s"),
			*FMD5::HashAnsiString(*Canonical)
		);
		Result.bSucceeded = true;
	}
	return Result;
}

bool FLLMNPCSkeletonCapabilityBuilder::BuildModelViewJson(
	const FLLMNPCSkeletonCapabilitySnapshot& Snapshot,
	FString& OutJson,
	FString& OutError
)
{
	OutJson.Reset();
	OutError.Reset();
	if (
		Snapshot.SchemaVersion != CapabilitySchemaVersion ||
		Snapshot.ProfileId.IsNone() ||
		Snapshot.CapabilityHash.IsEmpty()
	)
	{
		OutError = TEXT("LLMNPC_CAPABILITY_SNAPSHOT_INVALID");
		return false;
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema_version"), Snapshot.SchemaVersion);
	Root->SetStringField(TEXT("profile_id"), Snapshot.ProfileId.ToString());
	Root->SetStringField(TEXT("profile_semantic_version"), Snapshot.ProfileSemanticVersion);
	Root->SetStringField(TEXT("skeleton_signature"), Snapshot.SkeletonSignature);
	Root->SetStringField(TEXT("control_manifest_version"), Snapshot.ControlManifestVersion);
	Root->SetStringField(TEXT("capability_hash"), Snapshot.CapabilityHash);
	Root->SetStringField(TEXT("generated_at"), Snapshot.GeneratedAt);

	TArray<TSharedPtr<FJsonValue>> Capabilities;
	Capabilities.Reserve(Snapshot.Capabilities.Num());
	for (const FLLMNPCSemanticCapability& Capability : Snapshot.Capabilities)
	{
		Capabilities.Add(MakeShared<FJsonValueObject>(CapabilityToModelJson(Capability)));
	}
	Root->SetArrayField(TEXT("capabilities"), Capabilities);

	TSharedRef<FJsonObject> GlobalLimits = MakeShared<FJsonObject>();
	GlobalLimits->SetNumberField(
		TEXT("max_action_duration_seconds"),
		Snapshot.GlobalLimits.MaxActionDurationSeconds
	);
	GlobalLimits->SetNumberField(
		TEXT("max_primitive_count"),
		Snapshot.GlobalLimits.MaxPrimitiveCount
	);
	GlobalLimits->SetArrayField(
		TEXT("allowed_body_regions"),
		CapabilityNamesToJson(Snapshot.GlobalLimits.AllowedBodyRegions)
	);
	GlobalLimits->SetBoolField(
		TEXT("allows_lower_body"),
		Snapshot.GlobalLimits.bAllowsLowerBody
	);
	GlobalLimits->SetBoolField(
		TEXT("allows_root_motion"),
		Snapshot.GlobalLimits.bAllowsRootMotion
	);
	Root->SetObjectField(TEXT("global_limits"), GlobalLimits);

	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		OutError = TEXT("LLMNPC_CAPABILITY_JSON_SERIALIZE_FAILED");
		return false;
	}

	FString RestrictedField;
	if (ModelViewContainsRestrictedFields(OutJson, RestrictedField))
	{
		OutJson.Reset();
		OutError = FString::Printf(
			TEXT("LLMNPC_CAPABILITY_MODEL_VIEW_RESTRICTED_FIELD:%s"),
			*RestrictedField
		);
		return false;
	}
	return true;
}

bool FLLMNPCSkeletonCapabilityBuilder::ModelViewContainsRestrictedFields(
	const FString& ModelViewJson,
	FString& OutRestrictedField
)
{
	OutRestrictedField.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ModelViewJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutRestrictedField = TEXT("invalid_json");
		return true;
	}

	const TSet<FString> RestrictedKeys = {
		TEXT("bone"),
		TEXT("bone_name"),
		TEXT("semantic_bone_map"),
		TEXT("axis"),
		TEXT("axis_basis"),
		TEXT("quaternion"),
		TEXT("rotation"),
		TEXT("transform"),
		TEXT("component_space"),
		TEXT("compact_pose_index"),
		TEXT("collision_component")
	};

	TFunction<bool(const TSharedPtr<FJsonValue>&)> ScanValue;
	TFunction<bool(const TSharedPtr<FJsonObject>&)> ScanObject;
	ScanObject = [&](const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
		{
			const FString LowerKey = Pair.Key.ToLower();
			if (RestrictedKeys.Contains(LowerKey))
			{
				OutRestrictedField = Pair.Key;
				return true;
			}
			if (ScanValue(Pair.Value))
			{
				return true;
			}
		}
		return false;
	};
	ScanValue = [&](const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Object)
		{
			return ScanObject(Value->AsObject());
		}
		if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
			{
				if (ScanValue(Entry))
				{
					return true;
				}
			}
		}
		return false;
	};

	return ScanObject(Root);
}
