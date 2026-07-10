#pragma once

#include "CoreMinimal.h"
#include "Dialogue/LLMNPCDialogueTypes.h"
#include "Templates/LLMNPCTemplateCompiler.h"

class ULLMNPCMotionTemplate;
class ULLMNPCTemplateLibrarySubsystem;

class LLMNPCACTIONLAYER_API FLLMNPCModelTurnParser
{
public:
	static bool Parse(
		const FString& JsonString,
		FLLMNPCModelTurnDecision& OutDecision,
		FString& OutError
	);
};

class LLMNPCACTIONLAYER_API FLLMNPCModelTurnValidator
{
public:
	static bool ValidateAndResolve(
		FLLMNPCModelTurnDecision& InOutDecision,
		const ULLMNPCTemplateLibrarySubsystem& TemplateLibrary,
		FName SkeletonProfileId,
		const ULLMNPCMotionTemplate*& OutTemplate,
		FLLMNPCTemplateModifiers& OutModifiers,
		FString& OutError
	);
};
