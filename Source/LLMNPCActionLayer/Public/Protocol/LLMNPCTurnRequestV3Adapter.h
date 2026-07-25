#pragma once

#include "CoreMinimal.h"
#include "Templates/LLMNPCTemplateCandidate.h"

class LLMNPCACTIONLAYER_API FLLMNPCTurnRequestV3Adapter
{
public:
	static bool BuildV2SafeCandidate(
		const FLLMNPCTemplateCandidate& Source,
		FLLMNPCTemplateCandidate& OutCandidate,
		FString& OutError
	);

	static void AdaptCandidatesForSchema(
		const FString& TargetSchemaVersion,
		const TArray<FLLMNPCTemplateCandidate>& SourceCandidates,
		TArray<FLLMNPCTemplateCandidate>& OutCandidates,
		TArray<FName>* OutExcludedSelectionIds = nullptr
	);

	static const FLLMNPCCandidateStyleOption* FindStyleOption(
		const FLLMNPCTemplateCandidate& Candidate,
		FName Style
	);

	static FString BuildCandidateCardPreviewJson(
		const FLLMNPCTemplateCandidate& Candidate
	);
};
