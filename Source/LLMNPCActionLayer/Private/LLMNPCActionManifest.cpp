#include "LLMNPCActionManifest.h"

const FLLMNPCActionTemplate* ULLMNPCActionManifest::FindTemplateById(const FString& ActionId) const
{
	return Templates.FindByPredicate(
		[&ActionId](const FLLMNPCActionTemplate& Template)
		{
			return Template.ActionId == ActionId;
		}
	);
}
