#pragma once

#include "CoreMinimal.h"

class LLMNPCACTIONLAYER_API FLLMNPCModelTurnContract
{
public:
	static const FString& GetResponseInstruction();
	static const FString& GetSelectionSafetyInstruction();

	static FString BuildCanonicalNoActionResponse(
		const FString& AssistantText,
		FName ReasonTag
	);
};
