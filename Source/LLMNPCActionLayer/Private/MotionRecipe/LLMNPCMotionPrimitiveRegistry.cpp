#include "MotionRecipe/LLMNPCMotionPrimitiveRegistry.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FLLMNPCMotionPrimitiveParameterSchema NumberParameter(
	FName ParameterId,
	double MinValue,
	double MaxValue,
	double DefaultValue,
	FName Unit,
	const TCHAR* Description,
	bool bInteger = false
)
{
	FLLMNPCMotionPrimitiveParameterSchema Parameter;
	Parameter.ParameterId = ParameterId;
	Parameter.ValueType = ELLMNPCMotionRecipeValueType::Number;
	Parameter.Unit = Unit;
	Parameter.bInteger = bInteger;
	Parameter.bHasDefault = true;
	Parameter.DefaultValue = FLLMNPCMotionRecipeValue::Number(DefaultValue);
	Parameter.MinValue = MinValue;
	Parameter.MaxValue = MaxValue;
	Parameter.Description = Description;
	return Parameter;
}

FLLMNPCMotionPrimitiveParameterSchema EnumParameter(
	FName ParameterId,
	const TArray<FString>& AllowedValues,
	const FString& DefaultValue,
	const TCHAR* Description
)
{
	FLLMNPCMotionPrimitiveParameterSchema Parameter;
	Parameter.ParameterId = ParameterId;
	Parameter.ValueType = ELLMNPCMotionRecipeValueType::String;
	Parameter.Unit = TEXT("enum");
	Parameter.bHasDefault = true;
	Parameter.DefaultValue =
		FLLMNPCMotionRecipeValue::String(DefaultValue);
	Parameter.AllowedStringValues = AllowedValues;
	Parameter.Description = Description;
	return Parameter;
}

FLLMNPCMotionPrimitiveDefinition MakeDefinition(
	FName PrimitiveId,
	FName BodyRegion,
	FName SolverId,
	const TCHAR* Description
)
{
	FLLMNPCMotionPrimitiveDefinition Definition;
	Definition.PrimitiveId = PrimitiveId;
	Definition.BodyRegion = BodyRegion;
	Definition.SolverId = SolverId;
	Definition.Description = Description;
	Definition.RequiredCapabilities = {PrimitiveId};
	Definition.AllowedTargetModes = {TEXT("none")};
	Definition.BlockedStates = {TEXT("dead"), TEXT("ragdoll")};
	return Definition;
}

void AddCommonMotionParameters(
	FLLMNPCMotionPrimitiveDefinition& Definition,
	double DefaultAmplitude = 0.65
)
{
	Definition.ParameterSchemas.Add(
		NumberParameter(
			TEXT("amplitude"),
			0.2,
			1.0,
			DefaultAmplitude,
			TEXT("normalized"),
			TEXT("Normalized expressive amplitude.")
		)
	);
	Definition.ParameterSchemas.Add(
		NumberParameter(
			TEXT("speed"),
			0.6,
			1.4,
			1.0,
			TEXT("multiplier"),
			TEXT("Bounded timing multiplier.")
		)
	);
}

TArray<TSharedPtr<FJsonValue>> StringValues(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Result;
	Result.Reserve(Values.Num());
	for (const FString& Value : Values)
	{
		Result.Add(MakeShared<FJsonValueString>(Value));
	}
	return Result;
}

TArray<TSharedPtr<FJsonValue>> NameValues(const TArray<FName>& Values)
{
	TArray<FString> Strings;
	Strings.Reserve(Values.Num());
	for (const FName Value : Values)
	{
		Strings.Add(Value.ToString());
	}
	return StringValues(Strings);
}

void SetDefaultValue(
	const FLLMNPCMotionRecipeValue& Value,
	const TSharedRef<FJsonObject>& Object
)
{
	switch (Value.Type)
	{
	case ELLMNPCMotionRecipeValueType::Number:
		Object->SetNumberField(TEXT("default"), Value.NumberValue);
		break;
	case ELLMNPCMotionRecipeValueType::String:
		Object->SetStringField(TEXT("default"), Value.StringValue);
		break;
	case ELLMNPCMotionRecipeValueType::Boolean:
		Object->SetBoolField(TEXT("default"), Value.bBooleanValue);
		break;
	default:
		break;
	}
}

bool SupportsCapabilitySide(
	const FLLMNPCSemanticCapability& Capability,
	FName RegistrySide
)
{
	if (RegistrySide == TEXT("none"))
	{
		return
			Capability.SupportedSides.Contains(TEXT("center")) ||
			Capability.SupportedSides.Contains(TEXT("both"));
	}
	return Capability.SupportedSides.Contains(RegistrySide);
}
}

const FLLMNPCMotionPrimitiveRegistry& FLLMNPCMotionPrimitiveRegistry::Get()
{
	static const FLLMNPCMotionPrimitiveRegistry Registry;
	return Registry;
}

FLLMNPCMotionPrimitiveRegistry::FLLMNPCMotionPrimitiveRegistry()
{
	for (
		const TTuple<FName, FName, const TCHAR*> HeadDefinition :
		{
			TTuple<FName, FName, const TCHAR*>(
				TEXT("head.nod"),
				TEXT("solver.head_nod.v1"),
				TEXT("Move the head vertically for acknowledgement or agreement.")
			),
			TTuple<FName, FName, const TCHAR*>(
				TEXT("head.shake"),
				TEXT("solver.head_shake.v1"),
				TEXT("Move the head laterally for disagreement or refusal.")
			),
			TTuple<FName, FName, const TCHAR*>(
				TEXT("head.tilt"),
				TEXT("solver.head_tilt.v1"),
				TEXT("Tilt the head for curiosity, empathy, or uncertainty.")
			)
		}
	)
	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			HeadDefinition.Get<0>(),
			TEXT("head"),
			HeadDefinition.Get<1>(),
			HeadDefinition.Get<2>()
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::RuntimeSafe;
		Definition.AllowedSides = {TEXT("none")};
		Definition.RequiredChannelPatterns = {TEXT("head")};
		AddCommonMotionParameters(Definition);
		if (Definition.PrimitiveId != TEXT("head.tilt"))
		{
			Definition.ParameterSchemas.Add(
				NumberParameter(
					TEXT("cycles"),
					1.0,
					4.0,
					1.0,
					TEXT("count"),
					TEXT("Whole bounded repetition count."),
					true
				)
			);
		}
		else
		{
			Definition.ParameterSchemas.Add(
				EnumParameter(
					TEXT("direction"),
					{TEXT("left"), TEXT("right")},
					TEXT("right"),
					TEXT("Semantic tilt direction.")
				)
			);
		}
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("gaze.track"),
			TEXT("gaze"),
			TEXT("solver.gaze_track.v1"),
			TEXT("Track one validated target slot with constrained gaze motion.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::RuntimeSafe;
		Definition.AllowedSides = {TEXT("none")};
		Definition.AllowedTargetModes = {TEXT("target_slot")};
		Definition.bTargetRequired = true;
		Definition.RequiredChannelPatterns = {TEXT("gaze"), TEXT("head")};
		Definition.ParameterSchemas = {
			NumberParameter(
				TEXT("engagement"),
				0.0,
				1.0,
				0.75,
				TEXT("normalized"),
				TEXT("Strength of target engagement.")
			)
		};
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("chest.lean"),
			TEXT("chest"),
			TEXT("solver.chest_lean.v1"),
			TEXT("Apply a small upper-body lean without moving the root or feet.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::RuntimeSafe;
		Definition.AllowedSides = {TEXT("none")};
		Definition.RequiredChannelPatterns = {TEXT("chest")};
		AddCommonMotionParameters(Definition, 0.35);
		Definition.ParameterSchemas.Add(
			EnumParameter(
				TEXT("direction"),
				{TEXT("forward"), TEXT("back")},
				TEXT("forward"),
				TEXT("Semantic lean direction.")
			)
		);
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("chest.turn"),
			TEXT("chest"),
			TEXT("solver.chest_turn.v1"),
			TEXT("Apply a bounded upper-body turn while locomotion keeps ownership.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::RuntimeSafe;
		Definition.AllowedSides = {TEXT("left"), TEXT("right")};
		Definition.RequiredChannelPatterns = {TEXT("chest")};
		Definition.MirroringPolicy = TEXT("semantic_side");
		AddCommonMotionParameters(Definition, 0.35);
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("shoulder.shrug"),
			TEXT("shoulders"),
			TEXT("solver.shoulder_shrug.manny.v1"),
			TEXT("Raise both shoulders with bounded chest, elbow, wrist, and relaxed-hand participation.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::AuthoringOnly;
		Definition.AllowedSides = {TEXT("none")};
		Definition.RequiredChannelPatterns = {
			TEXT("{side}_shoulder"),
			TEXT("{side}_arm_ik"),
			TEXT("{side}_hand_pose"),
			TEXT("chest")
		};
		Definition.MirroringPolicy = TEXT("bilateral_internal");
		Definition.MaxInstancesPerRecipe = 1;
		AddCommonMotionParameters(Definition, 0.75);
		Definition.ParameterSchemas.Append({
			NumberParameter(
				TEXT("torso_participation"),
				0.0,
				1.0,
				0.35,
				TEXT("normalized"),
				TEXT("Amount of subtle upper-chest support.")
			),
			NumberParameter(
				TEXT("arm_openness"),
				0.2,
				1.0,
				0.6,
				TEXT("normalized"),
				TEXT("How far the elbows and hands open from the torso.")
			),
			NumberParameter(
				TEXT("palm_openness"),
				0.2,
				1.0,
				0.75,
				TEXT("normalized"),
				TEXT("Relaxed palm and finger openness.")
			),
			NumberParameter(
				TEXT("asymmetry"),
				0.0,
				0.25,
				0.05,
				TEXT("normalized"),
				TEXT("Small bounded left-right variation.")
			)
		});
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("arm.reach"),
			TEXT("arms"),
			TEXT("solver.arm_reach.v1"),
			TEXT("Move one hand toward a validated target slot.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::RuntimeSafe;
		Definition.AllowedSides = {TEXT("left"), TEXT("right")};
		Definition.AllowedTargetModes = {TEXT("target_slot")};
		Definition.bTargetRequired = true;
		Definition.RequiredChannelPatterns = {TEXT("{side}_arm_ik")};
		Definition.MirroringPolicy = TEXT("semantic_side");
		Definition.BlockedStates.Append({
			TEXT("left_hand_busy"),
			TEXT("right_hand_busy"),
			TEXT("two_hand_interaction")
		});
		Definition.ParameterSchemas = {
			NumberParameter(
				TEXT("reach"),
				0.05,
				0.98,
				0.75,
				TEXT("normalized"),
				TEXT("Bounded fraction of arm reach.")
			),
			NumberParameter(
				TEXT("height"),
				0.0,
				1.0,
				0.5,
				TEXT("normalized"),
				TEXT("Relative target height influence.")
			)
		};
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("arm.present"),
			TEXT("arms"),
			TEXT("solver.arm_present.manny.v2"),
			TEXT("Present a validated person, object, or direction with one open palm facing upward.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::AuthoringOnly;
		Definition.AllowedSides = {TEXT("left"), TEXT("right")};
		Definition.AllowedTargetModes = {TEXT("target_slot")};
		Definition.bTargetRequired = true;
		Definition.RequiredCapabilities.Add(TEXT("hand.pose.open"));
		Definition.RequiredChannelPatterns = {
			TEXT("{side}_arm_ik"),
			TEXT("{side}_hand_pose")
		};
		Definition.MirroringPolicy = TEXT("semantic_side");
		Definition.ParameterSchemas.Add(
			NumberParameter(
				TEXT("amplitude"),
				0.2,
				1.0,
				0.65,
				TEXT("normalized"),
				TEXT("How far and clearly the presenting arm opens toward the target.")
			)
		);
		Definition.ParameterSchemas.Add(
			NumberParameter(
				TEXT("height"),
				0.0,
				1.0,
				0.5,
				TEXT("normalized"),
				TEXT("Relative presentation height.")
			)
		);
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("hand.wave_arc"),
			TEXT("hands"),
			TEXT("solver.hand_wave_arc.v1"),
			TEXT("Raise one hand and move it through a bounded greeting or farewell arc.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::RuntimeSafe;
		Definition.AllowedSides = {TEXT("left"), TEXT("right")};
		Definition.AllowedTargetModes = {TEXT("none"), TEXT("target_slot")};
		Definition.RequiredCapabilities.Append({
			TEXT("arm.reach"),
			TEXT("hand.pose.open")
		});
		Definition.RequiredChannelPatterns = {
			TEXT("{side}_arm_ik"),
			TEXT("{side}_hand_pose")
		};
		Definition.MirroringPolicy = TEXT("semantic_side");
		Definition.BlockedStates.Append({
			TEXT("left_hand_busy"),
			TEXT("right_hand_busy"),
			TEXT("two_hand_interaction")
		});
		AddCommonMotionParameters(Definition, 0.65);
		Definition.ParameterSchemas.Append({
			NumberParameter(
				TEXT("height"),
				0.25,
				0.9,
				0.55,
				TEXT("normalized"),
				TEXT("Relative hand height.")
			),
			NumberParameter(
				TEXT("cycles"),
				1.0,
				4.0,
				2.0,
				TEXT("count"),
				TEXT("Whole bounded wave count."),
				true
			)
		});
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("hand.beckon"),
			TEXT("hands"),
			TEXT("solver.hand_beckon.manny.v1"),
			TEXT("Invite one validated target closer with a palm-up hand and bounded relaxed-to-curl finger cycles.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::AuthoringOnly;
		Definition.AllowedSides = {TEXT("left"), TEXT("right")};
		Definition.AllowedTargetModes = {TEXT("target_slot")};
		Definition.bTargetRequired = true;
		Definition.RequiredCapabilities.Append({
			TEXT("arm.reach"),
			TEXT("hand.pose.relaxed"),
			TEXT("hand.pose.curl")
		});
		Definition.RequiredChannelPatterns = {
			TEXT("{side}_arm_ik"),
			TEXT("{side}_hand_pose")
		};
		Definition.MirroringPolicy = TEXT("semantic_side");
		Definition.MinDurationSeconds = 0.9;
		Definition.MaxDurationSeconds = 3.2;
		Definition.MaxInstancesPerRecipe = 1;
		Definition.BlockedStates.Append({
			TEXT("left_hand_busy"),
			TEXT("right_hand_busy"),
			TEXT("two_hand_interaction")
		});
		Definition.ParameterSchemas = {
			NumberParameter(
				TEXT("amplitude"),
				0.3,
				1.0,
				0.7,
				TEXT("normalized"),
				TEXT("Overall bounded beckon expressiveness.")
			),
			NumberParameter(
				TEXT("speed"),
				0.7,
				1.3,
				1.0,
				TEXT("multiplier"),
				TEXT("Bounded finger curl timing within each authored cycle.")
			),
			NumberParameter(
				TEXT("cycles"),
				1.0,
				4.0,
				2.0,
				TEXT("count"),
				TEXT("Whole bounded invitation count."),
				true
			),
			NumberParameter(
				TEXT("curl_amount"),
				0.35,
				0.95,
				0.72,
				TEXT("normalized"),
				TEXT("Maximum calibrated curl-pose contribution.")
			),
			NumberParameter(
				TEXT("reach"),
				0.35,
				0.78,
				0.58,
				TEXT("normalized"),
				TEXT("Bounded fraction of arm reach toward the target.")
			),
			NumberParameter(
				TEXT("height"),
				0.3,
				0.8,
				0.55,
				TEXT("normalized"),
				TEXT("Relative invitation-hand height.")
			)
		};
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("hand.thumbs_up"),
			TEXT("hands"),
			TEXT("solver.hand_thumbs_up.manny.v1"),
			TEXT("Raise one hand near the upper chest with the thumb up and the other four fingers safely curled to signal approval or agreement.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::AuthoringOnly;
		Definition.AllowedSides = {TEXT("left"), TEXT("right")};
		Definition.AllowedTargetModes = {TEXT("none")};
		Definition.RequiredCapabilities.Add(TEXT("hand.pose.thumbs_up"));
		Definition.RequiredChannelPatterns = {
			TEXT("{side}_arm_ik"),
			TEXT("{side}_hand_pose")
		};
		Definition.MirroringPolicy = TEXT("semantic_side");
		Definition.MinDurationSeconds = 0.8;
		Definition.MaxDurationSeconds = 2.6;
		Definition.MaxInstancesPerRecipe = 1;
		Definition.BlockedStates.Append({
			TEXT("left_hand_busy"),
			TEXT("right_hand_busy"),
			TEXT("two_hand_interaction")
		});
		Definition.ParameterSchemas = {
			NumberParameter(
				TEXT("amplitude"),
				0.3,
				1.0,
				0.65,
				TEXT("normalized"),
				TEXT("Overall bounded approval-gesture clarity and arm participation.")
			),
			NumberParameter(
				TEXT("height"),
				0.3,
				0.8,
				0.55,
				TEXT("normalized"),
				TEXT("Relative hand height between the upper chest and shoulder.")
			)
		};
		Definitions.Add(MoveTemp(Definition));
	}

	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			TEXT("hands.contact"),
			TEXT("hands"),
			TEXT("solver.hands_contact.manny.v3"),
			TEXT("Bring both open hands together for UE-authored rhythmic contact and release.")
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::AuthoringOnly;
		Definition.AllowedSides = {TEXT("none")};
		Definition.RequiredCapabilities.Add(TEXT("hand.pose.open"));
		Definition.RequiredChannelPatterns = {
			TEXT("{side}_arm_ik"),
			TEXT("{side}_hand_pose")
		};
		Definition.MirroringPolicy = TEXT("bilateral_internal");
		Definition.MinDurationSeconds = 0.8;
		Definition.MaxDurationSeconds = 3.2;
		Definition.MaxInstancesPerRecipe = 1;
		Definition.BlockedStates.Append({
			TEXT("left_hand_busy"),
			TEXT("right_hand_busy"),
			TEXT("two_hand_interaction")
		});
		Definition.ParameterSchemas = {
			NumberParameter(
				TEXT("amplitude"),
				0.3,
				1.0,
				0.75,
				TEXT("normalized"),
				TEXT("Overall bounded clap expressiveness.")
			),
			NumberParameter(
				TEXT("speed"),
				0.7,
				1.3,
				1.0,
				TEXT("multiplier"),
				TEXT("Bounded hand-stroke speed and energy multiplier; duration and cycles own the contact cadence.")
			),
			NumberParameter(
				TEXT("cycles"),
				1.0,
				3.0,
				2.0,
				TEXT("count"),
				TEXT("Whole bounded hand-contact count."),
				true
			),
			NumberParameter(
				TEXT("contact_height"),
				0.35,
				0.75,
				0.55,
				TEXT("normalized"),
				TEXT("Relative contact height in front of the upper chest.")
			),
			NumberParameter(
				TEXT("separation"),
				0.2,
				1.0,
				0.65,
				TEXT("normalized"),
				TEXT("Distance between contacts without changing the contact point.")
			),
			NumberParameter(
				TEXT("palm_openness"),
				0.5,
				1.0,
				0.9,
				TEXT("normalized"),
				TEXT("Calibrated open-palm and finger participation.")
			)
		};
		Definitions.Add(MoveTemp(Definition));
	}

	for (
		const TTuple<FName, FName, const TCHAR*> PoseDefinition :
		{
			TTuple<FName, FName, const TCHAR*>(
				TEXT("hand.pose.open"),
				TEXT("solver.hand_pose_open.v1"),
				TEXT("Blend one hand toward its calibrated open pose.")
			),
			TTuple<FName, FName, const TCHAR*>(
				TEXT("hand.pose.point"),
				TEXT("solver.hand_pose_point.v1"),
				TEXT("Blend one hand toward its calibrated pointing pose.")
			),
			TTuple<FName, FName, const TCHAR*>(
				TEXT("hand.pose.relaxed"),
				TEXT("solver.hand_pose_relaxed.v1"),
				TEXT("Blend one hand toward its calibrated relaxed pose.")
			),
			TTuple<FName, FName, const TCHAR*>(
				TEXT("hand.pose.curl"),
				TEXT("solver.hand_pose_curl.v1"),
				TEXT("Blend one hand toward its calibrated curl pose.")
			),
			TTuple<FName, FName, const TCHAR*>(
				TEXT("hand.pose.thumbs_up"),
				TEXT("solver.hand_pose_thumbs_up.manny.v1"),
				TEXT("Blend one hand toward its calibrated thumbs-up pose.")
			)
		}
	)
	{
		FLLMNPCMotionPrimitiveDefinition Definition = MakeDefinition(
			PoseDefinition.Get<0>(),
			TEXT("hands"),
			PoseDefinition.Get<1>(),
			PoseDefinition.Get<2>()
		);
		Definition.Availability =
			ELLMNPCMotionPrimitiveAvailability::RuntimeSafe;
		Definition.AllowedSides = {TEXT("left"), TEXT("right")};
		Definition.RequiredChannelPatterns = {TEXT("{side}_hand_pose")};
		Definition.MirroringPolicy = TEXT("semantic_side");
		Definition.ParameterSchemas = {
			NumberParameter(
				TEXT("weight"),
				0.0,
				1.0,
				1.0,
				TEXT("normalized"),
				TEXT("Calibrated pose blend weight.")
			)
		};
		Definitions.Add(MoveTemp(Definition));
	}

	Definitions.Sort(
		[](const FLLMNPCMotionPrimitiveDefinition& A,
		   const FLLMNPCMotionPrimitiveDefinition& B)
		{
			return A.PrimitiveId.LexicalLess(B.PrimitiveId);
		}
	);
	for (FLLMNPCMotionPrimitiveDefinition& Definition : Definitions)
	{
		Definition.AllowedSides.Sort(FNameLexicalLess());
		Definition.AllowedTargetModes.Sort(FNameLexicalLess());
		Definition.RequiredCapabilities.Sort(FNameLexicalLess());
		Definition.RequiredChannelPatterns.Sort(FNameLexicalLess());
		Definition.BlockedStates.Sort(FNameLexicalLess());
		Definition.ParameterSchemas.Sort(
			[](const FLLMNPCMotionPrimitiveParameterSchema& A,
			   const FLLMNPCMotionPrimitiveParameterSchema& B)
			{
				return A.ParameterId.LexicalLess(B.ParameterId);
			}
		);
	}
}

const FString& FLLMNPCMotionPrimitiveRegistry::GetRegistryVersion() const
{
	return RegistryVersion;
}

const TArray<FLLMNPCMotionPrimitiveDefinition>&
FLLMNPCMotionPrimitiveRegistry::GetDefinitions() const
{
	return Definitions;
}

const FLLMNPCMotionPrimitiveDefinition*
FLLMNPCMotionPrimitiveRegistry::Find(FName PrimitiveId) const
{
	return Definitions.FindByPredicate(
		[PrimitiveId](const FLLMNPCMotionPrimitiveDefinition& Candidate)
		{
			return Candidate.PrimitiveId == PrimitiveId;
		}
	);
}

void FLLMNPCMotionPrimitiveRegistry::ResolveChannels(
	const FLLMNPCMotionPrimitiveDefinition& Definition,
	FName Side,
	TArray<FName>& OutChannels
) const
{
	OutChannels.Reset();
	for (const FName PatternName : Definition.RequiredChannelPatterns)
	{
		const FString Pattern = PatternName.ToString();
		if (!Pattern.Contains(TEXT("{side}")))
		{
			OutChannels.AddUnique(PatternName);
			continue;
		}

		if (Side == TEXT("none"))
		{
			OutChannels.AddUnique(
				FName(*Pattern.Replace(TEXT("{side}"), TEXT("left")))
			);
			OutChannels.AddUnique(
				FName(*Pattern.Replace(TEXT("{side}"), TEXT("right")))
			);
		}
		else
		{
			OutChannels.AddUnique(
				FName(*Pattern.Replace(TEXT("{side}"), *Side.ToString()))
			);
		}
	}
	OutChannels.Sort(FNameLexicalLess());
}

bool FLLMNPCMotionPrimitiveRegistry::IsDefinitionSupported(
	const FLLMNPCMotionPrimitiveDefinition& Definition,
	const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot
) const
{
	for (const FName RequiredCapability : Definition.RequiredCapabilities)
	{
		if (
			!CapabilitySnapshot.Capabilities.ContainsByPredicate(
				[RequiredCapability](
					const FLLMNPCSemanticCapability& Capability)
				{
					return Capability.CapabilityId == RequiredCapability;
				}
			)
		)
		{
			return false;
		}
	}

	const FLLMNPCSemanticCapability* PrimaryCapability =
		CapabilitySnapshot.Capabilities.FindByPredicate(
			[&Definition](const FLLMNPCSemanticCapability& Capability)
			{
				return Capability.CapabilityId == Definition.PrimitiveId;
			}
		);
	if (!PrimaryCapability)
	{
		return false;
	}
	for (const FName Side : Definition.AllowedSides)
	{
		if (SupportsCapabilitySide(*PrimaryCapability, Side))
		{
			return true;
		}
	}
	return false;
}

bool FLLMNPCMotionPrimitiveRegistry::BuildModelSchemaJson(
	const FLLMNPCSkeletonCapabilitySnapshot* CapabilitySnapshot,
	FString& OutJson,
	FString& OutError
) const
{
	OutJson.Reset();
	OutError.Reset();

	const double MaxDuration = CapabilitySnapshot
		? FMath::Min(
			static_cast<double>(
				CapabilitySnapshot->GlobalLimits.MaxActionDurationSeconds
			),
			static_cast<double>(
				LLMNPCMotionRecipe::DefaultMaxDurationSeconds
			)
		)
		: LLMNPCMotionRecipe::DefaultMaxDurationSeconds;
	const int32 MaxPrimitiveCount = CapabilitySnapshot
		? FMath::Min(
			CapabilitySnapshot->GlobalLimits.MaxPrimitiveCount,
			LLMNPCMotionRecipe::DefaultMaxPrimitiveCount
		)
		: LLMNPCMotionRecipe::DefaultMaxPrimitiveCount;

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(
		TEXT("$schema"),
		TEXT("https://json-schema.org/draft/2020-12/schema")
	);
	Root->SetStringField(
		TEXT("$id"),
		TEXT("urn:llmnpc:motion-recipe:v1")
	);
	Root->SetStringField(TEXT("title"), TEXT("LLM NPC Motion Recipe v1"));
	Root->SetStringField(TEXT("type"), TEXT("object"));
	Root->SetBoolField(TEXT("additionalProperties"), false);
	Root->SetStringField(
		TEXT("x-llmnpc-registry-version"),
		RegistryVersion
	);
	if (CapabilitySnapshot)
	{
		Root->SetStringField(
			TEXT("x-llmnpc-capability-hash"),
			CapabilitySnapshot->CapabilityHash
		);
	}
	Root->SetArrayField(
		TEXT("required"),
		StringValues({
			TEXT("schema_version"),
			TEXT("recipe_id"),
			TEXT("intent"),
			TEXT("duration"),
			TEXT("interruptible"),
			TEXT("primitives")
		})
	);

	const TSharedRef<FJsonObject> RootProperties = MakeShared<FJsonObject>();
	{
		const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
		Field->SetStringField(TEXT("type"), TEXT("string"));
		Field->SetStringField(
			TEXT("const"),
			LLMNPCMotionRecipe::SchemaVersion
		);
		RootProperties->SetObjectField(TEXT("schema_version"), Field);
	}
	for (const FString FieldName : {FString(TEXT("recipe_id")), FString(TEXT("intent"))})
	{
		const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
		Field->SetStringField(TEXT("type"), TEXT("string"));
		Field->SetNumberField(TEXT("minLength"), 1);
		Field->SetNumberField(TEXT("maxLength"), 128);
		Field->SetStringField(TEXT("pattern"), TEXT("^[A-Za-z0-9._-]+$"));
		RootProperties->SetObjectField(FieldName, Field);
	}
	{
		const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
		Field->SetStringField(TEXT("type"), TEXT("number"));
		Field->SetNumberField(TEXT("exclusiveMinimum"), 0.0);
		Field->SetNumberField(TEXT("maximum"), MaxDuration);
		RootProperties->SetObjectField(TEXT("duration"), Field);
	}
	{
		const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
		Field->SetStringField(TEXT("type"), TEXT("boolean"));
		Field->SetBoolField(TEXT("const"), true);
		RootProperties->SetObjectField(TEXT("interruptible"), Field);
	}

	TArray<TSharedPtr<FJsonValue>> PrimitiveSchemas;
	for (const FLLMNPCMotionPrimitiveDefinition& Definition : Definitions)
	{
		if (
			Definition.Availability ==
				ELLMNPCMotionPrimitiveAvailability::Disabled ||
			(
				CapabilitySnapshot &&
				!IsDefinitionSupported(Definition, *CapabilitySnapshot)
			)
		)
		{
			continue;
		}

		const TSharedRef<FJsonObject> PrimitiveSchema =
			MakeShared<FJsonObject>();
		PrimitiveSchema->SetStringField(TEXT("type"), TEXT("object"));
		PrimitiveSchema->SetBoolField(TEXT("additionalProperties"), false);
		PrimitiveSchema->SetStringField(
			TEXT("description"),
			Definition.Description
		);
		PrimitiveSchema->SetNumberField(
			TEXT("x-min-duration-seconds"),
			Definition.MinDurationSeconds
		);
		PrimitiveSchema->SetNumberField(
			TEXT("x-max-duration-seconds"),
			Definition.MaxDurationSeconds
		);

		TArray<FString> RequiredFields = {
			TEXT("primitive_id"),
			TEXT("start"),
			TEXT("end"),
			TEXT("parameters")
		};
		if (!Definition.AllowedSides.Contains(TEXT("none")))
		{
			RequiredFields.Add(TEXT("side"));
		}
		if (Definition.bTargetRequired)
		{
			RequiredFields.Add(TEXT("target_slot"));
		}
		PrimitiveSchema->SetArrayField(
			TEXT("required"),
			StringValues(RequiredFields)
		);

		const TSharedRef<FJsonObject> Properties = MakeShared<FJsonObject>();
		{
			const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
			Field->SetStringField(TEXT("type"), TEXT("string"));
			Field->SetStringField(
				TEXT("const"),
				Definition.PrimitiveId.ToString()
			);
			Properties->SetObjectField(TEXT("primitive_id"), Field);
		}
		{
			const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
			Field->SetStringField(TEXT("type"), TEXT("string"));
			Field->SetArrayField(
				TEXT("enum"),
				NameValues(Definition.AllowedSides)
			);
			if (Definition.AllowedSides.Contains(TEXT("none")))
			{
				Field->SetStringField(TEXT("default"), TEXT("none"));
			}
			Properties->SetObjectField(TEXT("side"), Field);
		}
		for (const FString TimeField : {FString(TEXT("start")), FString(TEXT("end"))})
		{
			const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
			Field->SetStringField(TEXT("type"), TEXT("number"));
			Field->SetNumberField(TEXT("minimum"), 0.0);
			Field->SetNumberField(TEXT("maximum"), MaxDuration);
			Properties->SetObjectField(TimeField, Field);
		}
		if (Definition.AllowedTargetModes.Contains(TEXT("target_slot")))
		{
			const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
			Field->SetStringField(TEXT("type"), TEXT("string"));
			Field->SetNumberField(TEXT("minLength"), 1);
			Field->SetNumberField(TEXT("maxLength"), 64);
			Field->SetStringField(
				TEXT("pattern"),
				TEXT("^[A-Za-z0-9._-]+$")
			);
			Properties->SetObjectField(TEXT("target_slot"), Field);
		}

		const TSharedRef<FJsonObject> ParametersSchema =
			MakeShared<FJsonObject>();
		ParametersSchema->SetStringField(TEXT("type"), TEXT("object"));
		ParametersSchema->SetBoolField(TEXT("additionalProperties"), false);
		const TSharedRef<FJsonObject> ParameterProperties =
			MakeShared<FJsonObject>();
		TArray<FString> RequiredParameters;
		for (
			const FLLMNPCMotionPrimitiveParameterSchema& Parameter :
			Definition.ParameterSchemas
		)
		{
			const TSharedRef<FJsonObject> Field = MakeShared<FJsonObject>();
			switch (Parameter.ValueType)
			{
			case ELLMNPCMotionRecipeValueType::Number:
				Field->SetStringField(
					TEXT("type"),
					Parameter.bInteger ? TEXT("integer") : TEXT("number")
				);
				Field->SetNumberField(TEXT("minimum"), Parameter.MinValue);
				Field->SetNumberField(TEXT("maximum"), Parameter.MaxValue);
				break;
			case ELLMNPCMotionRecipeValueType::String:
				Field->SetStringField(TEXT("type"), TEXT("string"));
				Field->SetArrayField(
					TEXT("enum"),
					StringValues(Parameter.AllowedStringValues)
				);
				break;
			case ELLMNPCMotionRecipeValueType::Boolean:
				Field->SetStringField(TEXT("type"), TEXT("boolean"));
				break;
			default:
				OutError = TEXT("LLMNPC_RECIPE_SCHEMA_PARAMETER_UNSUPPORTED");
				return false;
			}
			Field->SetStringField(TEXT("description"), Parameter.Description);
			Field->SetStringField(TEXT("x-unit"), Parameter.Unit.ToString());
			if (Parameter.bHasDefault)
			{
				SetDefaultValue(Parameter.DefaultValue, Field);
			}
			if (Parameter.bRequired)
			{
				RequiredParameters.Add(Parameter.ParameterId.ToString());
			}
			ParameterProperties->SetObjectField(
				Parameter.ParameterId.ToString(),
				Field
			);
		}
		ParametersSchema->SetObjectField(
			TEXT("properties"),
			ParameterProperties
		);
		if (!RequiredParameters.IsEmpty())
		{
			ParametersSchema->SetArrayField(
				TEXT("required"),
				StringValues(RequiredParameters)
			);
		}
		Properties->SetObjectField(TEXT("parameters"), ParametersSchema);
		PrimitiveSchema->SetObjectField(TEXT("properties"), Properties);
		PrimitiveSchemas.Add(MakeShared<FJsonValueObject>(PrimitiveSchema));
	}

	if (PrimitiveSchemas.IsEmpty())
	{
		OutError = TEXT("LLMNPC_RECIPE_SCHEMA_NO_SUPPORTED_PRIMITIVES");
		return false;
	}
	const TSharedRef<FJsonObject> PrimitiveItems = MakeShared<FJsonObject>();
	PrimitiveItems->SetArrayField(TEXT("oneOf"), PrimitiveSchemas);
	const TSharedRef<FJsonObject> Primitives = MakeShared<FJsonObject>();
	Primitives->SetStringField(TEXT("type"), TEXT("array"));
	Primitives->SetNumberField(TEXT("minItems"), 1);
	Primitives->SetNumberField(TEXT("maxItems"), MaxPrimitiveCount);
	Primitives->SetObjectField(TEXT("items"), PrimitiveItems);
	RootProperties->SetObjectField(TEXT("primitives"), Primitives);
	Root->SetObjectField(TEXT("properties"), RootProperties);

	const TSharedRef<TJsonWriter<>> Writer =
		TJsonWriterFactory<>::Create(&OutJson);
	if (!FJsonSerializer::Serialize(Root, Writer))
	{
		OutError = TEXT("LLMNPC_RECIPE_SCHEMA_SERIALIZE_FAILED");
		OutJson.Reset();
		return false;
	}
	return true;
}
