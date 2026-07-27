#include "MotionRecipe/LLMNPCMotionRecipeParser.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool ValidateRecipeFields(
	const TSharedPtr<FJsonObject>& Object,
	const TSet<FString>& AllowedFields,
	const TCHAR* ErrorPrefix,
	FString& OutError
)
{
	if (!Object.IsValid())
	{
		OutError = FString::Printf(TEXT("%s_OBJECT_MISSING"), ErrorPrefix);
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		if (!AllowedFields.Contains(Pair.Key))
		{
			OutError = FString::Printf(
				TEXT("%s_FIELD_UNKNOWN:%s"),
				ErrorPrefix,
				*Pair.Key
			);
			return false;
		}
	}
	return true;
}

bool GetRequiredRecipeString(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	FString& OutValue,
	FString& OutError
)
{
	if (!Object->TryGetStringField(Field, OutValue))
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_FIELD_MISSING:%s"),
			Field
		);
		return false;
	}
	OutValue = OutValue.TrimStartAndEnd();
	return true;
}

bool GetRequiredFiniteNumber(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field,
	double& OutValue,
	FString& OutError
)
{
	if (
		!Object->TryGetNumberField(Field, OutValue) ||
		!FMath::IsFinite(OutValue)
	)
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_NUMBER_INVALID:%s"),
			Field
		);
		return false;
	}
	return true;
}

bool IsSafeIdentifier(const FString& Value, int32 MaxLength)
{
	if (Value.IsEmpty() || Value.Len() > MaxLength)
	{
		return false;
	}
	for (const TCHAR Character : Value)
	{
		if (
			!FChar::IsAlnum(Character) &&
			Character != TEXT('.') &&
			Character != TEXT('_') &&
			Character != TEXT('-')
		)
		{
			return false;
		}
	}
	return true;
}

bool ParseParameterValue(
	const FString& ParameterId,
	const TSharedPtr<FJsonValue>& JsonValue,
	FLLMNPCMotionRecipeValue& OutValue,
	FString& OutError
)
{
	if (!JsonValue.IsValid())
	{
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_PARAMETER_NULL:%s"),
			*ParameterId
		);
		return false;
	}

	switch (JsonValue->Type)
	{
	case EJson::Number:
		{
			const double Number = JsonValue->AsNumber();
			if (!FMath::IsFinite(Number))
			{
				OutError = FString::Printf(
					TEXT("LLMNPC_RECIPE_PARAMETER_NON_FINITE:%s"),
					*ParameterId
				);
				return false;
			}
			OutValue = FLLMNPCMotionRecipeValue::Number(Number);
			return true;
		}
	case EJson::String:
		{
			const FString StringValue = JsonValue->AsString().TrimStartAndEnd();
			if (StringValue.Len() > 128)
			{
				OutError = FString::Printf(
					TEXT("LLMNPC_RECIPE_PARAMETER_STRING_TOO_LONG:%s"),
					*ParameterId
				);
				return false;
			}
			OutValue = FLLMNPCMotionRecipeValue::String(StringValue);
			return true;
		}
	case EJson::Boolean:
		OutValue = FLLMNPCMotionRecipeValue::Boolean(JsonValue->AsBool());
		return true;
	default:
		OutError = FString::Printf(
			TEXT("LLMNPC_RECIPE_PARAMETER_TYPE_UNSUPPORTED:%s"),
			*ParameterId
		);
		return false;
	}
}
}

bool FLLMNPCMotionRecipeParser::Parse(
	const FString& JsonString,
	FLLMNPCMotionRecipe& OutRecipe,
	FString& OutError
)
{
	OutRecipe = FLLMNPCMotionRecipe();
	OutError.Reset();
	if (JsonString.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("LLMNPC_RECIPE_EMPTY");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("LLMNPC_RECIPE_JSON_INVALID");
		return false;
	}

	static const TSet<FString> RootFields = {
		TEXT("schema_version"),
		TEXT("recipe_id"),
		TEXT("intent"),
		TEXT("duration"),
		TEXT("interruptible"),
		TEXT("primitives")
	};
	if (!ValidateRecipeFields(
		Root,
		RootFields,
		TEXT("LLMNPC_RECIPE_ROOT"),
		OutError
	))
	{
		return false;
	}

	if (
		!GetRequiredRecipeString(
			Root,
			TEXT("schema_version"),
			OutRecipe.SchemaVersion,
			OutError
		) ||
		OutRecipe.SchemaVersion != LLMNPCMotionRecipe::SchemaVersion
	)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("LLMNPC_RECIPE_SCHEMA_UNSUPPORTED");
		}
		return false;
	}

	if (
		!GetRequiredRecipeString(
			Root,
			TEXT("recipe_id"),
			OutRecipe.RecipeId,
			OutError
		) ||
		!IsSafeIdentifier(OutRecipe.RecipeId, 128)
	)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("LLMNPC_RECIPE_ID_INVALID");
		}
		return false;
	}
	if (
		!GetRequiredRecipeString(
			Root,
			TEXT("intent"),
			OutRecipe.Intent,
			OutError
		) ||
		!IsSafeIdentifier(OutRecipe.Intent, 128)
	)
	{
		if (OutError.IsEmpty())
		{
			OutError = TEXT("LLMNPC_RECIPE_INTENT_INVALID");
		}
		return false;
	}
	if (
		!GetRequiredFiniteNumber(
			Root,
			TEXT("duration"),
			OutRecipe.DurationSeconds,
			OutError
		)
	)
	{
		return false;
	}
	OutRecipe.DurationSeconds =
		FMath::RoundToDouble(OutRecipe.DurationSeconds * 1000.0) / 1000.0;

	if (!Root->TryGetBoolField(TEXT("interruptible"), OutRecipe.bInterruptible))
	{
		OutError = TEXT("LLMNPC_RECIPE_INTERRUPTIBLE_MISSING");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* PrimitiveValues = nullptr;
	if (
		!Root->TryGetArrayField(TEXT("primitives"), PrimitiveValues) ||
		!PrimitiveValues
	)
	{
		OutError = TEXT("LLMNPC_RECIPE_PRIMITIVES_MISSING");
		return false;
	}

	static const TSet<FString> PrimitiveFields = {
		TEXT("primitive_id"),
		TEXT("side"),
		TEXT("start"),
		TEXT("end"),
		TEXT("target_slot"),
		TEXT("parameters")
	};
	static const TSet<FString> ValidSides = {
		TEXT("left"),
		TEXT("right"),
		TEXT("auto"),
		TEXT("none")
	};

	OutRecipe.Primitives.Reserve(PrimitiveValues->Num());
	for (int32 Index = 0; Index < PrimitiveValues->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& PrimitiveValue =
			(*PrimitiveValues)[Index];
		if (
			!PrimitiveValue.IsValid() ||
			PrimitiveValue->Type != EJson::Object
		)
		{
			OutError = TEXT("LLMNPC_RECIPE_PRIMITIVE_OBJECT_REQUIRED");
			return false;
		}
		const TSharedPtr<FJsonObject> PrimitiveObject =
			PrimitiveValue->AsObject();
		if (
			!ValidateRecipeFields(
				PrimitiveObject,
				PrimitiveFields,
				TEXT("LLMNPC_RECIPE_PRIMITIVE"),
				OutError
			)
		)
		{
			return false;
		}

		FLLMNPCMotionRecipePrimitive Primitive;
		Primitive.SourceIndex = Index;
		FString StringValue;
		if (
			!GetRequiredRecipeString(
				PrimitiveObject,
				TEXT("primitive_id"),
				StringValue,
				OutError
			) ||
			!IsSafeIdentifier(StringValue, 96)
		)
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("LLMNPC_RECIPE_PRIMITIVE_ID_INVALID");
			}
			return false;
		}
		Primitive.PrimitiveId = FName(*StringValue);

		if (PrimitiveObject->HasField(TEXT("side")))
		{
			if (
				!PrimitiveObject->TryGetStringField(TEXT("side"), StringValue) ||
				!ValidSides.Contains(StringValue)
			)
			{
				OutError = TEXT("LLMNPC_RECIPE_SIDE_INVALID");
				return false;
			}
			Primitive.Side = FName(*StringValue);
		}

		if (
			!GetRequiredFiniteNumber(
				PrimitiveObject,
				TEXT("start"),
				Primitive.StartTimeSeconds,
				OutError
			) ||
			!GetRequiredFiniteNumber(
				PrimitiveObject,
				TEXT("end"),
				Primitive.EndTimeSeconds,
				OutError
			)
		)
		{
			return false;
		}
		Primitive.StartTimeSeconds =
			FMath::RoundToDouble(Primitive.StartTimeSeconds * 1000.0) / 1000.0;
		Primitive.EndTimeSeconds =
			FMath::RoundToDouble(Primitive.EndTimeSeconds * 1000.0) / 1000.0;

		if (PrimitiveObject->HasField(TEXT("target_slot")))
		{
			if (
				!PrimitiveObject->TryGetStringField(
					TEXT("target_slot"),
					StringValue
				) ||
				!IsSafeIdentifier(StringValue, 64)
			)
			{
				OutError = TEXT("LLMNPC_RECIPE_TARGET_SLOT_INVALID");
				return false;
			}
			Primitive.TargetSlot = FName(*StringValue);
		}

		const TSharedPtr<FJsonObject>* ParametersObject = nullptr;
		if (
			!PrimitiveObject->TryGetObjectField(
				TEXT("parameters"),
				ParametersObject
			) ||
			!ParametersObject ||
			!ParametersObject->IsValid()
		)
		{
			OutError = TEXT("LLMNPC_RECIPE_PARAMETERS_MISSING");
			return false;
		}
		for (
			const TPair<FString, TSharedPtr<FJsonValue>>& Pair :
			(*ParametersObject)->Values
		)
		{
			if (!IsSafeIdentifier(Pair.Key, 64))
			{
				OutError = FString::Printf(
					TEXT("LLMNPC_RECIPE_PARAMETER_ID_INVALID:%s"),
					*Pair.Key
				);
				return false;
			}
			FLLMNPCMotionRecipeValue ParameterValue;
			if (
				!ParseParameterValue(
					Pair.Key,
					Pair.Value,
					ParameterValue,
					OutError
				)
			)
			{
				return false;
			}
			Primitive.Parameters.Add(FName(*Pair.Key), MoveTemp(ParameterValue));
		}
		OutRecipe.Primitives.Add(MoveTemp(Primitive));
	}
	return true;
}
