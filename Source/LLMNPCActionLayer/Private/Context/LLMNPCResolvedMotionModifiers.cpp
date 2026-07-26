#include "Context/LLMNPCResolvedMotionModifiers.h"

FString FLLMNPCModifierResolutionTrace::ToSummary() const
{
	TArray<FString> Parts;
	Parts.Reserve(Steps.Num() + 1);
	for (const FLLMNPCModifierResolutionStep& Step : Steps)
	{
		if (Step.Operation == TEXT("mirror"))
		{
			Parts.Add(FString::Printf(
				TEXT("%s:%s"),
				*Step.Stage.ToString(),
				*Step.Reason
			));
			continue;
		}
		Parts.Add(FString::Printf(
			TEXT("%s:%s %.3f->%.3f (%s)"),
			*Step.Stage.ToString(),
			*Step.Modifier.ToString(),
			Step.Before,
			Step.After,
			*Step.Reason
		));
	}
	Parts.Add(FString::Printf(TEXT("result=%s"), *ResultCode.ToString()));
	return FString::Join(Parts, TEXT(" | "));
}
