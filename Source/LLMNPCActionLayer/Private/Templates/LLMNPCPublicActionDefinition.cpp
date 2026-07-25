#include "Templates/LLMNPCPublicActionDefinition.h"

#include "Misc/SecureHash.h"
#include "Templates/LLMNPCActionVocabulary.h"

namespace
{
bool ValidateTextArray(const TArray<FString>& Values, int32 MaxLength)
{
	TSet<FString> Unique;
	for (const FString& Value : Values)
	{
		const FString Clean = Value.TrimStartAndEnd();
		if (
			Clean.IsEmpty() ||
			Clean.Len() > MaxLength ||
			Unique.Contains(Clean.ToLower())
		)
		{
			return false;
		}
		Unique.Add(Clean.ToLower());
	}
	return true;
}

FString JoinDefinitionNames(const TArray<FName>& Names)
{
	TArray<FString> Values;
	for (const FName Name : Names)
	{
		Values.Add(Name.ToString().ToLower());
	}
	Values.Sort();
	return FString::Join(Values, TEXT(","));
}

FString JoinDefinitionStrings(const TArray<FString>& Strings)
{
	TArray<FString> Values;
	for (const FString& Value : Strings)
	{
		Values.Add(Value.TrimStartAndEnd());
	}
	Values.Sort();
	return FString::Join(Values, TEXT("|"));
}
}

bool ULLMNPCPublicActionDefinition::IsPublished() const
{
	return ReviewState == ELLMNPCTemplateReviewState::Published;
}

bool ULLMNPCPublicActionDefinition::ValidateDefinition(
	const ULLMNPCActionVocabulary* Vocabulary,
	FString& OutError
) const
{
	OutError.Reset();
	const FString CleanSummary = SelectionSummary.TrimStartAndEnd();
	if (
		PublicActionId.IsNone() ||
		SemanticVersion.TrimStartAndEnd().IsEmpty() ||
		DefinitionRevision < 1 ||
		CatalogSchemaVersion != LLMNPCCatalog::PublicActionSchemaVersion
	)
	{
		OutError = TEXT("LLMNPC_PUBLIC_ACTION_HEADER_INVALID");
		return false;
	}
	if (CleanSummary.IsEmpty() || CleanSummary.Len() > 240)
	{
		OutError = TEXT("LLMNPC_PUBLIC_ACTION_SELECTION_SUMMARY_INVALID");
		return false;
	}
	if (
		SemanticEffectTags.IsEmpty() ||
		GestureFamily.IsNone() ||
		DefaultStyle.IsNone() ||
		!ValidateTextArray(SuitableWhen, 160) ||
		!ValidateTextArray(AvoidWhen, 160) ||
		!ValidateTextArray(SearchKeywords, 80)
	)
	{
		OutError = TEXT("LLMNPC_PUBLIC_ACTION_METADATA_INVALID");
		return false;
	}
	if (bRequiresTarget != !TargetCategoryTags.IsEmpty())
	{
		OutError = TEXT("LLMNPC_PUBLIC_ACTION_TARGET_CONTRACT_INVALID");
		return false;
	}
	if (Vocabulary)
	{
		if (
			!Vocabulary->IsTagAllowed(
				GestureFamily,
				ELLMNPCActionVocabularyField::GestureFamily
			) ||
			!Vocabulary->IsTagAllowed(
				DefaultStyle,
				ELLMNPCActionVocabularyField::VariantStyle
			) ||
			!Vocabulary->ValidateTags(
				SemanticEffectTags,
				ELLMNPCActionVocabularyField::SemanticEffect,
				OutError
			) ||
			!Vocabulary->ValidateTags(
				TargetCategoryTags,
				ELLMNPCActionVocabularyField::TargetCategory,
				OutError
			)
		)
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("LLMNPC_PUBLIC_ACTION_VOCABULARY_INVALID");
			}
			return false;
		}
	}
	if (!ContentHash.IsEmpty() && ContentHash != BuildContentHash(*this))
	{
		OutError = TEXT("LLMNPC_PUBLIC_ACTION_CONTENT_HASH_STALE");
		return false;
	}
	return true;
}

FString ULLMNPCPublicActionDefinition::BuildContentHash(
	const ULLMNPCPublicActionDefinition& Definition
)
{
	const FString Stable = FString::Printf(
		TEXT("%s\n%s\n%d\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%d\n%s"),
		*Definition.PublicActionId.ToString().ToLower(),
		*Definition.SemanticVersion.TrimStartAndEnd(),
		Definition.DefinitionRevision,
		*Definition.DisplayName.ToString().TrimStartAndEnd(),
		*Definition.SelectionSummary.TrimStartAndEnd(),
		*JoinDefinitionStrings(Definition.SuitableWhen),
		*JoinDefinitionStrings(Definition.AvoidWhen),
		*JoinDefinitionNames(Definition.SemanticEffectTags),
		*JoinDefinitionNames(Definition.TargetCategoryTags),
		*Definition.GestureFamily.ToString().ToLower(),
		*Definition.DefaultStyle.ToString().ToLower(),
		*JoinDefinitionNames(Definition.IncompatibleActionFamilies),
		Definition.bRequiresTarget ? 1 : 0,
		*Definition.CatalogSchemaVersion
	);
	return FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*(Stable + TEXT("\n") + JoinDefinitionStrings(Definition.SearchKeywords)))
	);
}

FPrimaryAssetId ULLMNPCPublicActionDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("LLMNPCPublicAction"), GetFName());
}
