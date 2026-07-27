#include "MotionRecipe/LLMNPCMotionRecipeTypes.h"

#include "Misc/SecureHash.h"
#include "Serialization/JsonWriter.h"

namespace
{
double NormalizeNumber(double Value)
{
	const double Rounded = FMath::RoundToDouble(Value * 10000.0) / 10000.0;
	return FMath::IsNearlyZero(Rounded) ? 0.0 : Rounded;
}
}

FLLMNPCMotionRecipeValue FLLMNPCMotionRecipeValue::Number(double Value)
{
	FLLMNPCMotionRecipeValue Result;
	Result.Type = ELLMNPCMotionRecipeValueType::Number;
	Result.NumberValue = Value;
	return Result;
}

FLLMNPCMotionRecipeValue FLLMNPCMotionRecipeValue::String(const FString& Value)
{
	FLLMNPCMotionRecipeValue Result;
	Result.Type = ELLMNPCMotionRecipeValueType::String;
	Result.StringValue = Value;
	return Result;
}

FLLMNPCMotionRecipeValue FLLMNPCMotionRecipeValue::Boolean(bool bValue)
{
	FLLMNPCMotionRecipeValue Result;
	Result.Type = ELLMNPCMotionRecipeValueType::Boolean;
	Result.bBooleanValue = bValue;
	return Result;
}

const FLLMNPCMotionRecipeValue* FLLMNPCMotionRecipePrimitive::FindParameter(
	FName ParameterId
) const
{
	return Parameters.Find(ParameterId);
}

double FLLMNPCMotionRecipePrimitive::GetNumberParameter(
	FName ParameterId,
	double DefaultValue
) const
{
	const FLLMNPCMotionRecipeValue* Value = FindParameter(ParameterId);
	return Value && Value->Type == ELLMNPCMotionRecipeValueType::Number
		? Value->NumberValue
		: DefaultValue;
}

FString FLLMNPCMotionRecipePrimitive::GetStringParameter(
	FName ParameterId,
	const FString& DefaultValue
) const
{
	const FLLMNPCMotionRecipeValue* Value = FindParameter(ParameterId);
	return Value && Value->Type == ELLMNPCMotionRecipeValueType::String
		? Value->StringValue
		: DefaultValue;
}

bool FLLMNPCMotionRecipePrimitive::GetBooleanParameter(
	FName ParameterId,
	bool bDefaultValue
) const
{
	const FLLMNPCMotionRecipeValue* Value = FindParameter(ParameterId);
	return Value && Value->Type == ELLMNPCMotionRecipeValueType::Boolean
		? Value->bBooleanValue
		: bDefaultValue;
}

const FLLMNPCMotionPrimitiveParameterSchema*
FLLMNPCMotionPrimitiveDefinition::FindParameterSchema(FName ParameterId) const
{
	return ParameterSchemas.FindByPredicate(
		[ParameterId](const FLLMNPCMotionPrimitiveParameterSchema& Candidate)
		{
			return Candidate.ParameterId == ParameterId;
		}
	);
}

bool FLLMNPCMotionRecipeCanonicalizer::BuildCanonicalJson(
	const FLLMNPCMotionRecipe& Recipe,
	FString& OutJson,
	FString& OutError
)
{
	OutJson.Reset();
	OutError.Reset();
	if (Recipe.SchemaVersion != LLMNPCMotionRecipe::SchemaVersion)
	{
		OutError = TEXT("LLMNPC_RECIPE_CANONICAL_SCHEMA_UNSUPPORTED");
		return false;
	}

	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(
			&OutJson
		);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("schema_version"), Recipe.SchemaVersion);
	Writer->WriteValue(TEXT("recipe_id"), Recipe.RecipeId);
	Writer->WriteValue(TEXT("intent"), Recipe.Intent);
	Writer->WriteValue(
		TEXT("duration"),
		FMath::RoundToDouble(Recipe.DurationSeconds * 1000.0) / 1000.0
	);
	Writer->WriteValue(TEXT("interruptible"), Recipe.bInterruptible);
	Writer->WriteArrayStart(TEXT("primitives"));
	for (const FLLMNPCMotionRecipePrimitive& Primitive : Recipe.Primitives)
	{
		Writer->WriteObjectStart();
		Writer->WriteValue(
			TEXT("primitive_id"),
			Primitive.PrimitiveId.ToString()
		);
		Writer->WriteValue(TEXT("side"), Primitive.Side.ToString());
		Writer->WriteValue(
			TEXT("start"),
			FMath::RoundToDouble(Primitive.StartTimeSeconds * 1000.0) / 1000.0
		);
		Writer->WriteValue(
			TEXT("end"),
			FMath::RoundToDouble(Primitive.EndTimeSeconds * 1000.0) / 1000.0
		);
		if (!Primitive.TargetSlot.IsNone())
		{
			Writer->WriteValue(
				TEXT("target_slot"),
				Primitive.TargetSlot.ToString()
			);
		}
		Writer->WriteObjectStart(TEXT("parameters"));
		TArray<FName> ParameterIds;
		Primitive.Parameters.GetKeys(ParameterIds);
		ParameterIds.Sort(
			[](const FName& A, const FName& B)
			{
				return A.LexicalLess(B);
			}
		);
		for (const FName ParameterId : ParameterIds)
		{
			const FLLMNPCMotionRecipeValue& Value =
				Primitive.Parameters.FindChecked(ParameterId);
			switch (Value.Type)
			{
			case ELLMNPCMotionRecipeValueType::Number:
				Writer->WriteValue(
					ParameterId.ToString(),
					NormalizeNumber(Value.NumberValue)
				);
				break;
			case ELLMNPCMotionRecipeValueType::String:
				Writer->WriteValue(
					ParameterId.ToString(),
					Value.StringValue
				);
				break;
			case ELLMNPCMotionRecipeValueType::Boolean:
				Writer->WriteValue(
					ParameterId.ToString(),
					Value.bBooleanValue
				);
				break;
			default:
				OutError = TEXT("LLMNPC_RECIPE_CANONICAL_VALUE_UNSUPPORTED");
				return false;
			}
		}
		Writer->WriteObjectEnd();
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	if (!Writer->Close())
	{
		OutError = TEXT("LLMNPC_RECIPE_CANONICAL_SERIALIZE_FAILED");
		OutJson.Reset();
		return false;
	}
	return true;
}

FString FLLMNPCMotionRecipeCanonicalizer::BuildRecipeHash(
	const FString& CanonicalRecipeJson
)
{
	return FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*CanonicalRecipeJson)
	);
}
