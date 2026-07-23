#pragma once

#include "CoreMinimal.h"

class LLMNPCACTIONLAYER_API FLLMNPCProtocolCompatibility
{
public:
	static const FString& CurrentTurnRequestSchema();
	static const FString& CurrentModelTurnSchema();
	static const FString& CurrentSelectionPrompt();
	static const FString& CurrentMotionPlanVersion();

	static bool IsSupportedTurnRequestSchema(const FString& Version);
	static bool IsSupportedModelTurnSchema(const FString& Version);
	static bool IsSupportedSelectionPrompt(const FString& Version);
	static bool NormalizeMotionPlanVersion(FString& InOutVersion, FString& OutError);
};
