#pragma once

#include "CoreMinimal.h"

class FJsonObject;

class FLLMNPCOnlineReportSanitizer
{
public:
	static bool SanitizeAndSerialize(
		const TSharedRef<FJsonObject>& Source,
		FString& OutJson
	);

	static bool IsForbiddenFieldName(const FString& FieldName);
};
