#include "Selection/LLMNPCSelectionAnalyticsSubsystem.h"

#include "LLMNPCSettings.h"
#include "Misc/Crc.h"

void ULLMNPCSelectionAnalyticsSubsystem::BeginSelection(
	const FGuid& RequestId,
	FName NPCId,
	FName ProviderId,
	const FString& PromptVersion,
	const TArray<FName>& OfferedSelectionIds,
	int32 ExcludedCandidateCount,
	const FString& ContextJson
)
{
	FLLMNPCSelectionAnalyticsEvent& Event = RecentEvents.AddDefaulted_GetRef();
	Event.RequestId = RequestId;
	Event.TimestampSeconds = FPlatformTime::Seconds();
	Event.NPCId = NPCId;
	Event.ProviderId = ProviderId;
	Event.PromptVersion = PromptVersion;
	Event.OfferedSelectionIds = OfferedSelectionIds;
	Event.ExcludedCandidateCount = FMath::Max(0, ExcludedCandidateCount);
	Event.ContextHash = static_cast<int64>(FCrc::StrCrc32(*ContextJson));

	const ULLMNPCSettings* Settings = GetDefault<ULLMNPCSettings>();
	const int32 MaxEvents = Settings ? FMath::Clamp(Settings->MaxSelectionAnalyticsEvents, 8, 2048) : 128;
	if (RecentEvents.Num() > MaxEvents)
	{
		RecentEvents.RemoveAt(0, RecentEvents.Num() - MaxEvents);
	}
}

void ULLMNPCSelectionAnalyticsSubsystem::CompleteSelection(
	const FGuid& RequestId,
	FName SelectedActionId,
	FName ResolvedTemplateId,
	FName Outcome,
	FName ErrorCode,
	bool bUsedFallback
)
{
	for (int32 Index = RecentEvents.Num() - 1; Index >= 0; --Index)
	{
		FLLMNPCSelectionAnalyticsEvent& Event = RecentEvents[Index];
		if (Event.RequestId == RequestId)
		{
			Event.SelectedActionId = SelectedActionId;
			Event.ResolvedTemplateId = ResolvedTemplateId;
			Event.Outcome = Outcome.IsNone() ? FName(TEXT("unknown")) : Outcome;
			Event.ErrorCode = ErrorCode;
			Event.bUsedFallback = bUsedFallback;
			return;
		}
	}
}

void ULLMNPCSelectionAnalyticsSubsystem::ClearEvents()
{
	RecentEvents.Reset();
}
