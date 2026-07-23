#include "Protocol/LLMNPCProtocolCompatibility.h"

namespace
{
const FString TurnRequestSchema(TEXT("llmnpc.turn_request.v2"));
const FString ModelTurnSchema(TEXT("llmnpc.model_turn.v1"));
const FString SelectionPrompt(TEXT("llmnpc.selection_prompt.v3"));
const FString MotionPlanVersion(TEXT("1.0"));
}

const FString& FLLMNPCProtocolCompatibility::CurrentTurnRequestSchema()
{
	return TurnRequestSchema;
}

const FString& FLLMNPCProtocolCompatibility::CurrentModelTurnSchema()
{
	return ModelTurnSchema;
}

const FString& FLLMNPCProtocolCompatibility::CurrentSelectionPrompt()
{
	return SelectionPrompt;
}

const FString& FLLMNPCProtocolCompatibility::CurrentMotionPlanVersion()
{
	return MotionPlanVersion;
}

bool FLLMNPCProtocolCompatibility::IsSupportedTurnRequestSchema(const FString& Version)
{
	return Version == TurnRequestSchema;
}

bool FLLMNPCProtocolCompatibility::IsSupportedModelTurnSchema(const FString& Version)
{
	return Version == ModelTurnSchema;
}

bool FLLMNPCProtocolCompatibility::IsSupportedSelectionPrompt(const FString& Version)
{
	return
		Version == TEXT("llmnpc.selection_prompt.v1") ||
		Version == TEXT("llmnpc.selection_prompt.v2") ||
		Version == SelectionPrompt;
}

bool FLLMNPCProtocolCompatibility::NormalizeMotionPlanVersion(
	FString& InOutVersion,
	FString& OutError
)
{
	OutError.Reset();
	InOutVersion = InOutVersion.TrimStartAndEnd();
	if (
		InOutVersion == MotionPlanVersion ||
		InOutVersion == TEXT("1") ||
		InOutVersion == TEXT("1.0.0")
	)
	{
		InOutVersion = MotionPlanVersion;
		return true;
	}

	OutError = FString::Printf(
		TEXT("LLMNPC_MOTION_PLAN_VERSION_UNSUPPORTED:%s"),
		*InOutVersion
	);
	return false;
}
