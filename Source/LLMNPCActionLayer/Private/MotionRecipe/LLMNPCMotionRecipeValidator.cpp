#include "MotionRecipe/LLMNPCMotionRecipeValidator.h"

namespace
{
struct FChannelOccupancy
{
	FName Channel = NAME_None;
	double StartTimeSeconds = 0.0;
	double EndTimeSeconds = 0.0;
	int32 PrimitiveIndex = INDEX_NONE;
	ELLMNPCMotionPrimitiveOverlapPolicy OverlapPolicy =
		ELLMNPCMotionPrimitiveOverlapPolicy::Exclusive;
};

bool Fail(
	FLLMNPCMotionRecipeValidationResult& OutResult,
	const FString& ErrorCode,
	int32 PrimitiveIndex = INDEX_NONE
)
{
	OutResult.bValid = false;
	OutResult.ErrorCode = ErrorCode;
	OutResult.PrimitiveIndex = PrimitiveIndex;
	return false;
}

const FLLMNPCSemanticCapability* FindCapability(
	const FLLMNPCSkeletonCapabilitySnapshot& Snapshot,
	FName CapabilityId
)
{
	return Snapshot.Capabilities.FindByPredicate(
		[CapabilityId](const FLLMNPCSemanticCapability& Candidate)
		{
			return Candidate.CapabilityId == CapabilityId;
		}
	);
}

bool CapabilitySupportsSide(
	const FLLMNPCSemanticCapability& Capability,
	FName Side
)
{
	if (Side == TEXT("none"))
	{
		return
			Capability.SupportedSides.Contains(TEXT("center")) ||
			Capability.SupportedSides.Contains(TEXT("both"));
	}
	return Capability.SupportedSides.Contains(Side);
}

bool IntervalsOverlap(
	double AStart,
	double AEnd,
	double BStart,
	double BEnd
)
{
	return AStart < BEnd && BStart < AEnd;
}

double QuantizeTime(double Value)
{
	const double Result = FMath::RoundToDouble(Value * 1000.0) / 1000.0;
	return FMath::IsNearlyZero(Result) ? 0.0 : Result;
}

double QuantizeParameter(double Value)
{
	const double Result = FMath::RoundToDouble(Value * 10000.0) / 10000.0;
	return FMath::IsNearlyZero(Result) ? 0.0 : Result;
}

FString ValueTypeName(ELLMNPCMotionRecipeValueType Type)
{
	switch (Type)
	{
	case ELLMNPCMotionRecipeValueType::Number:
		return TEXT("number");
	case ELLMNPCMotionRecipeValueType::String:
		return TEXT("string");
	case ELLMNPCMotionRecipeValueType::Boolean:
		return TEXT("boolean");
	default:
		return TEXT("unknown");
	}
}
}

bool FLLMNPCMotionRecipeValidator::ValidateAndNormalize(
	FLLMNPCMotionRecipe& InOutRecipe,
	const FLLMNPCSkeletonCapabilitySnapshot& CapabilitySnapshot,
	const FLLMNPCMotionPrimitiveRegistry& Registry,
	const FLLMNPCMotionRecipeValidationContext& Context,
	FLLMNPCMotionRecipeValidationResult& OutResult
)
{
	OutResult = FLLMNPCMotionRecipeValidationResult();
	if (Context.Mode != ELLMNPCMotionRecipeMode::AuthoringSandbox)
	{
		return Fail(OutResult, TEXT("LLMNPC_RECIPE_RUNTIME_DISABLED"));
	}
	if (
		InOutRecipe.SchemaVersion != LLMNPCMotionRecipe::SchemaVersion ||
		Registry.GetRegistryVersion() != LLMNPCMotionRecipe::RegistryVersion
	)
	{
		return Fail(OutResult, TEXT("LLMNPC_RECIPE_VERSION_MISMATCH"));
	}
	if (
		CapabilitySnapshot.SchemaVersion !=
			TEXT("llmnpc.skeleton_capability.v1") ||
		CapabilitySnapshot.CapabilityHash.IsEmpty()
	)
	{
		return Fail(OutResult, TEXT("LLMNPC_RECIPE_CAPABILITY_INVALID"));
	}
	if (!InOutRecipe.bInterruptible)
	{
		return Fail(OutResult, TEXT("LLMNPC_RECIPE_INTERRUPTIBLE_REQUIRED"));
	}

	InOutRecipe.DurationSeconds =
		QuantizeTime(InOutRecipe.DurationSeconds);
	const double MaxDuration = FMath::Min(
		static_cast<double>(FMath::Max(0.05f, Context.MaxDurationSeconds)),
		static_cast<double>(FMath::Max(
			0.05f,
			CapabilitySnapshot.GlobalLimits.MaxActionDurationSeconds
		))
	);
	if (
		InOutRecipe.DurationSeconds < 0.05 ||
		InOutRecipe.DurationSeconds > MaxDuration
	)
	{
		return Fail(OutResult, TEXT("LLMNPC_RECIPE_DURATION_OUT_OF_RANGE"));
	}

	const int32 MaxPrimitiveCount = FMath::Min(
		FMath::Max(1, Context.MaxPrimitiveCount),
		FMath::Max(1, CapabilitySnapshot.GlobalLimits.MaxPrimitiveCount)
	);
	if (
		InOutRecipe.Primitives.IsEmpty() ||
		InOutRecipe.Primitives.Num() > MaxPrimitiveCount
	)
	{
		return Fail(OutResult, TEXT("LLMNPC_RECIPE_PRIMITIVE_LIMIT"));
	}

	TMap<FString, int32> InstanceCounts;
	TArray<FChannelOccupancy> ChannelOccupancies;
	TSet<FName> RequiredChannelSet;
	TSet<FName> UsedTargetSlots;
	TSet<FName> AllBodyRegions;
	TArray<double> BodyRegionEventTimes = {0.0, InOutRecipe.DurationSeconds};

	for (
		int32 PrimitiveArrayIndex = 0;
		PrimitiveArrayIndex < InOutRecipe.Primitives.Num();
		++PrimitiveArrayIndex
	)
	{
		FLLMNPCMotionRecipePrimitive& Primitive =
			InOutRecipe.Primitives[PrimitiveArrayIndex];
		Primitive.SourceIndex = PrimitiveArrayIndex;
		const FLLMNPCMotionPrimitiveDefinition* Definition =
			Registry.Find(Primitive.PrimitiveId);
		if (!Definition)
		{
			return Fail(
				OutResult,
				FString::Printf(
					TEXT("LLMNPC_RECIPE_PRIMITIVE_UNKNOWN:%s"),
					*Primitive.PrimitiveId.ToString()
				),
				PrimitiveArrayIndex
			);
		}
		if (
			Definition->SchemaVersion != InOutRecipe.SchemaVersion ||
			Definition->Availability ==
				ELLMNPCMotionPrimitiveAvailability::Disabled
		)
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_PRIMITIVE_UNAVAILABLE"),
				PrimitiveArrayIndex
			);
		}
		if (!Definition->AllowedSides.Contains(Primitive.Side))
		{
			return Fail(
				OutResult,
				FString::Printf(
					TEXT("LLMNPC_RECIPE_SIDE_FORBIDDEN:%s"),
					*Primitive.Side.ToString()
				),
				PrimitiveArrayIndex
			);
		}

		Primitive.StartTimeSeconds =
			QuantizeTime(Primitive.StartTimeSeconds);
		Primitive.EndTimeSeconds =
			QuantizeTime(Primitive.EndTimeSeconds);
		if (
			Primitive.StartTimeSeconds < 0.0 ||
			Primitive.StartTimeSeconds >= Primitive.EndTimeSeconds ||
			Primitive.EndTimeSeconds > InOutRecipe.DurationSeconds
		)
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_PRIMITIVE_TIME_INVALID"),
				PrimitiveArrayIndex
			);
		}
		if (
			Primitive.EndTimeSeconds - Primitive.StartTimeSeconds >
			Definition->MaxDurationSeconds
		)
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_PRIMITIVE_DURATION_EXCEEDED"),
				PrimitiveArrayIndex
			);
		}

		for (const FName BlockedState : Definition->BlockedStates)
		{
			if (Context.ActiveBlockedStates.Contains(BlockedState))
			{
				return Fail(
					OutResult,
					FString::Printf(
						TEXT("LLMNPC_RECIPE_STATE_BLOCKED:%s"),
						*BlockedState.ToString()
					),
					PrimitiveArrayIndex
				);
			}
		}

		const bool bHasTarget = !Primitive.TargetSlot.IsNone();
		if (Definition->bTargetRequired && !bHasTarget)
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_TARGET_SLOT_REQUIRED"),
				PrimitiveArrayIndex
			);
		}
		if (
			bHasTarget &&
			!Definition->AllowedTargetModes.Contains(TEXT("target_slot"))
		)
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_TARGET_SLOT_FORBIDDEN"),
				PrimitiveArrayIndex
			);
		}
		if (
			!bHasTarget &&
			!Definition->AllowedTargetModes.Contains(TEXT("none"))
		)
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_TARGET_MODE_INVALID"),
				PrimitiveArrayIndex
			);
		}
		if (bHasTarget)
		{
			if (!Context.AllowedTargetSlots.Contains(Primitive.TargetSlot))
			{
				return Fail(
					OutResult,
					FString::Printf(
						TEXT("LLMNPC_RECIPE_TARGET_SLOT_NOT_ALLOWED:%s"),
						*Primitive.TargetSlot.ToString()
					),
					PrimitiveArrayIndex
				);
			}
			UsedTargetSlots.Add(Primitive.TargetSlot);
		}

		for (const FName RequiredCapability : Definition->RequiredCapabilities)
		{
			if (!FindCapability(CapabilitySnapshot, RequiredCapability))
			{
				return Fail(
					OutResult,
					FString::Printf(
						TEXT("LLMNPC_RECIPE_CAPABILITY_MISSING:%s"),
						*RequiredCapability.ToString()
					),
					PrimitiveArrayIndex
				);
			}
		}
		const FLLMNPCSemanticCapability* PrimaryCapability =
			FindCapability(CapabilitySnapshot, Definition->PrimitiveId);
		if (
			!PrimaryCapability ||
			!CapabilitySupportsSide(*PrimaryCapability, Primitive.Side)
		)
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_CAPABILITY_SIDE_UNSUPPORTED"),
				PrimitiveArrayIndex
			);
		}
		const FName CapabilityTargetMode =
			bHasTarget ? FName(TEXT("scene_target")) : FName(TEXT("none"));
		if (!PrimaryCapability->TargetModes.Contains(CapabilityTargetMode))
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_CAPABILITY_TARGET_UNSUPPORTED"),
				PrimitiveArrayIndex
			);
		}
		if (
			!CapabilitySnapshot.GlobalLimits.AllowedBodyRegions.Contains(
				Definition->BodyRegion
			)
		)
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_BODY_REGION_FORBIDDEN"),
				PrimitiveArrayIndex
			);
		}

		for (
			TPair<FName, FLLMNPCMotionRecipeValue>& ParameterPair :
			Primitive.Parameters
		)
		{
			const FLLMNPCMotionPrimitiveParameterSchema* ParameterSchema =
				Definition->FindParameterSchema(ParameterPair.Key);
			if (!ParameterSchema)
			{
				return Fail(
					OutResult,
					FString::Printf(
						TEXT("LLMNPC_RECIPE_PARAMETER_UNKNOWN:%s"),
						*ParameterPair.Key.ToString()
					),
					PrimitiveArrayIndex
				);
			}
			FLLMNPCMotionRecipeValue& Value = ParameterPair.Value;
			if (Value.Type != ParameterSchema->ValueType)
			{
				return Fail(
					OutResult,
					FString::Printf(
						TEXT("LLMNPC_RECIPE_PARAMETER_TYPE:%s:%s"),
						*ParameterPair.Key.ToString(),
						*ValueTypeName(ParameterSchema->ValueType)
					),
					PrimitiveArrayIndex
				);
			}
			if (Value.Type == ELLMNPCMotionRecipeValueType::Number)
			{
				if (!FMath::IsFinite(Value.NumberValue))
				{
					return Fail(
						OutResult,
						TEXT("LLMNPC_RECIPE_PARAMETER_NON_FINITE"),
						PrimitiveArrayIndex
					);
				}
				Value.NumberValue = QuantizeParameter(Value.NumberValue);
				if (
					Value.NumberValue < ParameterSchema->MinValue ||
					Value.NumberValue > ParameterSchema->MaxValue
				)
				{
					return Fail(
						OutResult,
						FString::Printf(
							TEXT("LLMNPC_RECIPE_PARAMETER_OUT_OF_RANGE:%s"),
							*ParameterPair.Key.ToString()
						),
						PrimitiveArrayIndex
					);
				}
				if (
					ParameterSchema->bInteger &&
					!FMath::IsNearlyEqual(
						Value.NumberValue,
						FMath::RoundToDouble(Value.NumberValue),
						1.e-9
					)
				)
				{
					return Fail(
						OutResult,
						FString::Printf(
							TEXT("LLMNPC_RECIPE_PARAMETER_INTEGER_REQUIRED:%s"),
							*ParameterPair.Key.ToString()
						),
						PrimitiveArrayIndex
					);
				}
			}
			else if (
				Value.Type == ELLMNPCMotionRecipeValueType::String &&
				!ParameterSchema->AllowedStringValues.Contains(
					Value.StringValue
				)
			)
			{
				return Fail(
					OutResult,
					FString::Printf(
						TEXT("LLMNPC_RECIPE_PARAMETER_ENUM_INVALID:%s"),
						*ParameterPair.Key.ToString()
					),
					PrimitiveArrayIndex
				);
			}
		}

		for (
			const FLLMNPCMotionPrimitiveParameterSchema& ParameterSchema :
			Definition->ParameterSchemas
		)
		{
			if (Primitive.Parameters.Contains(ParameterSchema.ParameterId))
			{
				continue;
			}
			if (ParameterSchema.bRequired)
			{
				return Fail(
					OutResult,
					FString::Printf(
						TEXT("LLMNPC_RECIPE_PARAMETER_REQUIRED:%s"),
						*ParameterSchema.ParameterId.ToString()
					),
					PrimitiveArrayIndex
				);
			}
			if (ParameterSchema.bHasDefault)
			{
				Primitive.Parameters.Add(
					ParameterSchema.ParameterId,
					ParameterSchema.DefaultValue
				);
				OutResult.NormalizationRecords.Add(
					FString::Printf(
						TEXT("primitive[%d].parameters.%s=default"),
						PrimitiveArrayIndex,
						*ParameterSchema.ParameterId.ToString()
					)
				);
			}
		}

		const FString InstanceKey = FString::Printf(
			TEXT("%s|%s"),
			*Primitive.PrimitiveId.ToString(),
			*Primitive.Side.ToString()
		);
		int32& InstanceCount = InstanceCounts.FindOrAdd(InstanceKey);
		++InstanceCount;
		if (InstanceCount > Definition->MaxInstancesPerRecipe)
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_INSTANCE_LIMIT"),
				PrimitiveArrayIndex
			);
		}

		TArray<FName> ResolvedChannels;
		Registry.ResolveChannels(
			*Definition,
			Primitive.Side,
			ResolvedChannels
		);
		for (const FName Channel : ResolvedChannels)
		{
			RequiredChannelSet.Add(Channel);
			FChannelOccupancy& Occupancy =
				ChannelOccupancies.AddDefaulted_GetRef();
			Occupancy.Channel = Channel;
			Occupancy.StartTimeSeconds = Primitive.StartTimeSeconds;
			Occupancy.EndTimeSeconds = Primitive.EndTimeSeconds;
			Occupancy.PrimitiveIndex = PrimitiveArrayIndex;
			Occupancy.OverlapPolicy = Definition->OverlapPolicy;
		}
		AllBodyRegions.Add(Definition->BodyRegion);
		BodyRegionEventTimes.Add(Primitive.StartTimeSeconds);
		BodyRegionEventTimes.Add(Primitive.EndTimeSeconds);
	}

	const int32 MaxTargetCount = FMath::Max(0, Context.MaxTargetCount);
	if (UsedTargetSlots.Num() > MaxTargetCount)
	{
		return Fail(OutResult, TEXT("LLMNPC_RECIPE_TARGET_LIMIT"));
	}

	for (int32 A = 0; A < ChannelOccupancies.Num(); ++A)
	{
		for (int32 B = A + 1; B < ChannelOccupancies.Num(); ++B)
		{
			const FChannelOccupancy& Left = ChannelOccupancies[A];
			const FChannelOccupancy& Right = ChannelOccupancies[B];
			if (
				Left.PrimitiveIndex == Right.PrimitiveIndex ||
				Left.Channel != Right.Channel ||
				!IntervalsOverlap(
					Left.StartTimeSeconds,
					Left.EndTimeSeconds,
					Right.StartTimeSeconds,
					Right.EndTimeSeconds
				)
			)
			{
				continue;
			}
			if (
				Left.OverlapPolicy !=
					ELLMNPCMotionPrimitiveOverlapPolicy::Blend ||
				Right.OverlapPolicy !=
					ELLMNPCMotionPrimitiveOverlapPolicy::Blend
			)
			{
				return Fail(
					OutResult,
					FString::Printf(
						TEXT("LLMNPC_RECIPE_CHANNEL_CONFLICT:%s"),
						*Left.Channel.ToString()
					),
					Right.PrimitiveIndex
				);
			}
		}
	}

	BodyRegionEventTimes.Sort();
	for (int32 EventIndex = 0; EventIndex + 1 < BodyRegionEventTimes.Num(); ++EventIndex)
	{
		const double Start = BodyRegionEventTimes[EventIndex];
		const double End = BodyRegionEventTimes[EventIndex + 1];
		if (End - Start <= 1.e-9)
		{
			continue;
		}
		const double SampleTime = (Start + End) * 0.5;
		TSet<FName> ActiveRegions;
		for (
			int32 PrimitiveIndex = 0;
			PrimitiveIndex < InOutRecipe.Primitives.Num();
			++PrimitiveIndex
		)
		{
			const FLLMNPCMotionRecipePrimitive& Primitive =
				InOutRecipe.Primitives[PrimitiveIndex];
			if (
				SampleTime >= Primitive.StartTimeSeconds &&
				SampleTime < Primitive.EndTimeSeconds
			)
			{
				const FLLMNPCMotionPrimitiveDefinition* Definition =
					Registry.Find(Primitive.PrimitiveId);
				if (Definition)
				{
					ActiveRegions.Add(Definition->BodyRegion);
				}
			}
		}
		if (ActiveRegions.Num() > FMath::Max(1, Context.MaxActiveBodyRegions))
		{
			return Fail(
				OutResult,
				TEXT("LLMNPC_RECIPE_BODY_REGION_LIMIT")
			);
		}
	}

	OutResult.RequiredChannels = RequiredChannelSet.Array();
	OutResult.UsedTargetSlots = UsedTargetSlots.Array();
	OutResult.ActiveBodyRegions = AllBodyRegions.Array();
	OutResult.RequiredChannels.Sort(FNameLexicalLess());
	OutResult.UsedTargetSlots.Sort(FNameLexicalLess());
	OutResult.ActiveBodyRegions.Sort(FNameLexicalLess());
	OutResult.bValid = true;
	return true;
}
