#include "LLMNPCActionValidator.h"

namespace
{
const TArray<FLLMNPCActionTemplate>& GetBuiltInTemplates()
{
	static const TArray<FLLMNPCActionTemplate> Templates =
		[]()
		{
			TArray<FLLMNPCActionTemplate> Result;

			FLLMNPCActionTemplate LookAt;
			LookAt.ActionId = TEXT("gaze.look_at");
			LookAt.MinDuration = 0.2f;
			LookAt.MaxDuration = 3.0f;
			LookAt.bRequiresTarget = true;
			LookAt.bRequiresUpperBodyFree = false;
			Result.Add(LookAt);

			FLLMNPCActionTemplate Nod;
			Nod.ActionId = TEXT("gesture.nod");
			Nod.MinDuration = 0.2f;
			Nod.MaxDuration = 2.5f;
			Nod.bRequiresTarget = false;
			Result.Add(Nod);

			FLLMNPCActionTemplate Wave;
			Wave.ActionId = TEXT("gesture.wave");
			Wave.MinDuration = 0.4f;
			Wave.MaxDuration = 4.0f;
			Wave.bRequiresTarget = false;
			Result.Add(Wave);

			FLLMNPCActionTemplate Point;
			Point.ActionId = TEXT("gesture.point");
			Point.MinDuration = 0.3f;
			Point.MaxDuration = 3.0f;
			Point.bRequiresTarget = true;
			Result.Add(Point);

			return Result;
		}();

	return Templates;
}
}

bool ULLMNPCActionValidator::ValidateAndClamp(
	FLLMNPCActionRequest& Action,
	const ULLMNPCActionManifest* Manifest,
	FString& OutReason
) const
{
	OutReason.Reset();

	const FLLMNPCActionTemplate* Template = nullptr;
	if (Manifest)
	{
		Template = Manifest->FindTemplateById(Action.ActionId);
	}

	if (!Template)
	{
		Template = FindBuiltInTemplate(Action.ActionId);
	}

	if (!Template)
	{
		OutReason = FString::Printf(TEXT("Unknown ActionId: %s"), *Action.ActionId);
		return false;
	}

	Action.Amplitude = FMath::Clamp(Action.Amplitude, Template->MinAmplitude, Template->MaxAmplitude);
	Action.Speed = FMath::Clamp(Action.Speed, 0.1f, 2.0f);
	Action.Duration = FMath::Clamp(Action.Duration, Template->MinDuration, Template->MaxDuration);
	Action.Height = FMath::Clamp(Action.Height, 0.0f, 1.0f);
	Action.Beats = FMath::Clamp(Action.Beats, 1, 8);
	Action.Priority = FMath::Clamp(Action.Priority, 0.0f, 1.0f);

	if (Template->bRequiresTarget && Action.TargetRef.TrimStartAndEnd().IsEmpty())
	{
		OutReason = FString::Printf(TEXT("%s requires a TargetRef"), *Action.ActionId);
		return false;
	}

	return true;
}

bool ULLMNPCActionValidator::IsAllowedActionId(const FString& ActionId, const ULLMNPCActionManifest* Manifest) const
{
	if (Manifest && Manifest->FindTemplateById(ActionId))
	{
		return true;
	}

	return FindBuiltInTemplate(ActionId) != nullptr;
}

bool ULLMNPCActionValidator::IsTargetRequired(const FString& ActionId, const ULLMNPCActionManifest* Manifest)
{
	if (Manifest)
	{
		if (const FLLMNPCActionTemplate* Template = Manifest->FindTemplateById(ActionId))
		{
			return Template->bRequiresTarget;
		}
	}

	if (const FLLMNPCActionTemplate* Template = FindBuiltInTemplate(ActionId))
	{
		return Template->bRequiresTarget;
	}

	return false;
}

const FLLMNPCActionTemplate* ULLMNPCActionValidator::FindBuiltInTemplate(const FString& ActionId)
{
	return GetBuiltInTemplates().FindByPredicate(
		[&ActionId](const FLLMNPCActionTemplate& Template)
		{
			return Template.ActionId == ActionId;
		}
	);
}
