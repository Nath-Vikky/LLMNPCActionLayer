#include "Templates/LLMNPCTemplateSearchIndex.h"

#include "Misc/SecureHash.h"
#include "Templates/LLMNPCActionVocabulary.h"
#include "Templates/LLMNPCMotionTemplate.h"
#include "Templates/LLMNPCPublicActionDefinition.h"

namespace
{
void AddDiagnostic(
	TArray<FLLMNPCCatalogDiagnostic>& Diagnostics,
	FName Code,
	const UObject* Asset,
	const FString& FieldPath,
	const FString& Message
)
{
	FLLMNPCCatalogDiagnostic& Diagnostic = Diagnostics.AddDefaulted_GetRef();
	Diagnostic.Code = Code;
	Diagnostic.AssetPath = Asset ? Asset->GetPathName() : FString();
	Diagnostic.FieldPath = FieldPath;
	Diagnostic.Message = Message;
}

bool ParseSemVer(const FString& Version, int32& OutMajor, int32& OutMinor, int32& OutPatch)
{
	TArray<FString> Parts;
	Version.TrimStartAndEnd().ParseIntoArray(Parts, TEXT("."), true);
	if (Parts.Num() != 3)
	{
		return false;
	}
	return
		LexTryParseString(OutMajor, *Parts[0]) &&
		LexTryParseString(OutMinor, *Parts[1]) &&
		LexTryParseString(OutPatch, *Parts[2]) &&
		OutMajor >= 0 &&
		OutMinor >= 0 &&
		OutPatch >= 0;
}

int32 CompareVersion(const FString& A, const FString& B)
{
	int32 AMajor = 0;
	int32 AMinor = 0;
	int32 APatch = 0;
	int32 BMajor = 0;
	int32 BMinor = 0;
	int32 BPatch = 0;
	if (
		ParseSemVer(A, AMajor, AMinor, APatch) &&
		ParseSemVer(B, BMajor, BMinor, BPatch)
	)
	{
		if (AMajor != BMajor)
		{
			return AMajor < BMajor ? -1 : 1;
		}
		if (AMinor != BMinor)
		{
			return AMinor < BMinor ? -1 : 1;
		}
		if (APatch != BPatch)
		{
			return APatch < BPatch ? -1 : 1;
		}
		return 0;
	}
	return A.Compare(B, ESearchCase::CaseSensitive);
}

bool IsDefinitionNewer(
	const ULLMNPCPublicActionDefinition& Candidate,
	const ULLMNPCPublicActionDefinition& Current
)
{
	const int32 VersionComparison = CompareVersion(
		Candidate.SemanticVersion,
		Current.SemanticVersion
	);
	return VersionComparison > 0 ||
		(VersionComparison == 0 && Candidate.DefinitionRevision > Current.DefinitionRevision);
}

bool IsTemplateNewer(
	const ULLMNPCMotionTemplate& Candidate,
	const ULLMNPCMotionTemplate& Current
)
{
	const int32 VersionComparison = CompareVersion(
		Candidate.Metadata.SemanticVersion,
		Current.Metadata.SemanticVersion
	);
	return VersionComparison > 0 ||
		(VersionComparison == 0 &&
			Candidate.Metadata.CatalogRevision > Current.Metadata.CatalogRevision);
}

FString DefinitionVersionKey(const ULLMNPCPublicActionDefinition& Definition)
{
	return FString::Printf(
		TEXT("%s|%s|%d"),
		*Definition.PublicActionId.ToString().ToLower(),
		*Definition.SemanticVersion,
		Definition.DefinitionRevision
	);
}

FString TemplateVersionKey(const ULLMNPCMotionTemplate& Template)
{
	return FString::Printf(
		TEXT("%s|%s|%d"),
		*Template.Metadata.TemplateId.ToString().ToLower(),
		*Template.Metadata.SemanticVersion,
		Template.Metadata.CatalogRevision
	);
}

bool ValidateTemplateVocabulary(
	const ULLMNPCMotionTemplate& Template,
	const ULLMNPCActionVocabulary& Vocabulary,
	FString& OutError
)
{
	const FLLMNPCTemplateMetadata& Metadata = Template.Metadata;
	return
		Vocabulary.ValidateTags(
			Metadata.IntentTags,
			ELLMNPCActionVocabularyField::Intent,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.EmotionTags,
			ELLMNPCActionVocabularyField::Emotion,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.PersonalityTags,
			ELLMNPCActionVocabularyField::Personality,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.BodyRegionTags,
			ELLMNPCActionVocabularyField::BodyRegion,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.SpatialRequirementTags,
			ELLMNPCActionVocabularyField::SpatialRequirement,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.SemanticEffectTags,
			ELLMNPCActionVocabularyField::SemanticEffect,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.TargetCategoryTags,
			ELLMNPCActionVocabularyField::TargetCategory,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Metadata.VariantStyleTags,
			ELLMNPCActionVocabularyField::VariantStyle,
			OutError
		) &&
		Vocabulary.ValidateTags(
			Template.ModifierPolicy.AllowedStyleTags,
			ELLMNPCActionVocabularyField::VariantStyle,
			OutError
		);
}

TArray<FString> Tokenize(const FString& Text)
{
	FString Normalized = Text.ToLower();
	for (TCHAR& Character : Normalized)
	{
		if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
		{
			Character = TEXT(' ');
		}
	}
	TArray<FString> Tokens;
	Normalized.ParseIntoArrayWS(Tokens);
	Tokens.RemoveAll(
		[](const FString& Token)
		{
			return Token.Len() < 2;
		}
	);
	Tokens.Sort();
	for (int32 Index = Tokens.Num() - 1; Index > 0; --Index)
	{
		if (Tokens[Index] == Tokens[Index - 1])
		{
			Tokens.RemoveAt(Index);
		}
	}
	return Tokens;
}

void AddUniqueNames(TArray<FName>& Target, const TArray<FName>& Source)
{
	for (const FName Value : Source)
	{
		Target.AddUnique(Value);
	}
}

bool IntersectRange(FVector2D& InOutRange, const FVector2D& Other)
{
	InOutRange.X = FMath::Max(InOutRange.X, Other.X);
	InOutRange.Y = FMath::Min(InOutRange.Y, Other.Y);
	return InOutRange.X <= InOutRange.Y;
}

void AddOrIntersectStyle(
	TArray<FLLMNPCCandidateStyleOption>& StyleOptions,
	FName Style,
	const ULLMNPCMotionTemplate& Template
)
{
	if (Style.IsNone())
	{
		return;
	}
	FLLMNPCCandidateStyleOption* Existing = StyleOptions.FindByPredicate(
		[Style](const FLLMNPCCandidateStyleOption& Candidate)
		{
			return Candidate.Style == Style;
		}
	);
	if (!Existing)
	{
		FLLMNPCCandidateStyleOption& Added = StyleOptions.AddDefaulted_GetRef();
		Added.Style = Style;
		Added.AmplitudeRange = Template.ModifierPolicy.AmplitudeRange;
		Added.SpeedRange = Template.ModifierPolicy.SpeedRange;
		Added.DurationRange = Template.ModifierPolicy.DurationRange;
		Added.bMirrorAllowed = Template.ModifierPolicy.bAllowMirror;
		return;
	}
	const bool bValid =
		IntersectRange(Existing->AmplitudeRange, Template.ModifierPolicy.AmplitudeRange) &&
		IntersectRange(Existing->SpeedRange, Template.ModifierPolicy.SpeedRange) &&
		IntersectRange(Existing->DurationRange, Template.ModifierPolicy.DurationRange);
	Existing->bMirrorAllowed &= Template.ModifierPolicy.bAllowMirror;
	if (!bValid)
	{
		StyleOptions.RemoveAll(
			[Style](const FLLMNPCCandidateStyleOption& Candidate)
			{
				return Candidate.Style == Style;
			}
		);
	}
}
}

bool FLLMNPCTemplateSearchIndex::Build(
	const TArray<ULLMNPCMotionTemplate*>& Templates,
	const TArray<ULLMNPCPublicActionDefinition*>& Definitions,
	const ULLMNPCActionVocabulary* Vocabulary,
	const TSet<FName>& AvailableSkeletonProfiles
)
{
	Reset();
	FString Error;
	if (!Vocabulary || !Vocabulary->ValidateVocabulary(Error))
	{
		AddDiagnostic(
			Diagnostics,
			TEXT("LLMNPC_CATALOG_VOCABULARY_INVALID"),
			Vocabulary,
			TEXT("ActionVocabulary"),
			Error
		);
		return false;
	}

	TMap<FString, TArray<const ULLMNPCPublicActionDefinition*>> DefinitionVersions;
	for (const ULLMNPCPublicActionDefinition* Definition : Definitions)
	{
		if (!Definition || !Definition->IsPublished())
		{
			continue;
		}
		if (!Definition->ValidateDefinition(Vocabulary, Error))
		{
			AddDiagnostic(
				Diagnostics,
				TEXT("LLMNPC_CATALOG_DEFINITION_INVALID"),
				Definition,
				TEXT("PublicActionDefinition"),
				Error
			);
			continue;
		}
		DefinitionVersions.FindOrAdd(DefinitionVersionKey(*Definition)).Add(Definition);
	}

	for (const TPair<FString, TArray<const ULLMNPCPublicActionDefinition*>>& Pair : DefinitionVersions)
	{
		if (Pair.Value.Num() > 1)
		{
			for (const ULLMNPCPublicActionDefinition* Duplicate : Pair.Value)
			{
				AddDiagnostic(
					Diagnostics,
					TEXT("LLMNPC_CATALOG_DUPLICATE_PUBLIC_ACTION_VERSION"),
					Duplicate,
					TEXT("PublicActionId"),
					Pair.Key
				);
			}
			continue;
		}
		const ULLMNPCPublicActionDefinition* Definition = Pair.Value[0];
		const ULLMNPCPublicActionDefinition** Current =
			DefinitionIndex.Find(Definition->PublicActionId);
		if (!Current || IsDefinitionNewer(*Definition, **Current))
		{
			DefinitionIndex.Add(Definition->PublicActionId, Definition);
		}
	}

	TMap<FString, TArray<const ULLMNPCMotionTemplate*>> TemplateVersions;
	for (const ULLMNPCMotionTemplate* Template : Templates)
	{
		if (!Template || !Template->IsPublished())
		{
			continue;
		}
		if (!Template->ValidateTemplate(Error))
		{
			AddDiagnostic(
				Diagnostics,
				TEXT("LLMNPC_CATALOG_TEMPLATE_INVALID"),
				Template,
				TEXT("MotionTemplate"),
				Error
			);
			continue;
		}
		if (!AvailableSkeletonProfiles.Contains(Template->Metadata.SkeletonProfileId))
		{
			AddDiagnostic(
				Diagnostics,
				TEXT("LLMNPC_CATALOG_SKELETON_PROFILE_MISSING"),
				Template,
				TEXT("Metadata.SkeletonProfileId"),
				Template->Metadata.SkeletonProfileId.ToString()
			);
			continue;
		}
		if (!ValidateTemplateVocabulary(*Template, *Vocabulary, Error))
		{
			AddDiagnostic(
				Diagnostics,
				TEXT("LLMNPC_CATALOG_TEMPLATE_VOCABULARY_INVALID"),
				Template,
				TEXT("Metadata"),
				Error
			);
			continue;
		}
		const ULLMNPCPublicActionDefinition* Definition =
			FindDefinition(Template->Metadata.PublicActionId);
		if (!Definition)
		{
			AddDiagnostic(
				Diagnostics,
				TEXT("LLMNPC_CATALOG_PUBLIC_ACTION_NOT_PUBLISHED"),
				Template,
				TEXT("Metadata.PublicActionId"),
				Template->Metadata.PublicActionId.ToString()
			);
			continue;
		}
		if (
			Template->Metadata.bRequiresTarget != Definition->bRequiresTarget ||
			Template->Metadata.TargetCategoryTags != Definition->TargetCategoryTags
		)
		{
			AddDiagnostic(
				Diagnostics,
				TEXT("LLMNPC_CATALOG_TARGET_CONTRACT_MISMATCH"),
				Template,
				TEXT("Metadata.TargetCategoryTags"),
				Definition->PublicActionId.ToString()
			);
			continue;
		}
		TemplateVersions.FindOrAdd(TemplateVersionKey(*Template)).Add(Template);
	}

	for (const TPair<FString, TArray<const ULLMNPCMotionTemplate*>>& Pair : TemplateVersions)
	{
		if (Pair.Value.Num() > 1)
		{
			for (const ULLMNPCMotionTemplate* Duplicate : Pair.Value)
			{
				AddDiagnostic(
					Diagnostics,
					TEXT("LLMNPC_CATALOG_DUPLICATE_TEMPLATE_VERSION"),
					Duplicate,
					TEXT("Metadata.TemplateId"),
					Pair.Key
				);
			}
			continue;
		}
		const ULLMNPCMotionTemplate* Template = Pair.Value[0];
		const ULLMNPCMotionTemplate** Current =
			TemplateIndex.Find(Template->Metadata.TemplateId);
		if (!Current || IsTemplateNewer(*Template, **Current))
		{
			TemplateIndex.Add(Template->Metadata.TemplateId, Template);
		}
	}

	for (const TPair<FName, const ULLMNPCMotionTemplate*>& Pair : TemplateIndex)
	{
		const ULLMNPCMotionTemplate* Template = Pair.Value;
		if (Template->Metadata.bAllowRuntimeModelSelection)
		{
			PublicActionIndex.FindOrAdd(
				Template->Metadata.PublicActionId
			).Add(Template->Metadata.TemplateId);
		}
	}
	for (TPair<FName, TArray<FName>>& Pair : PublicActionIndex)
	{
		Pair.Value.Sort(FNameLexicalLess());
		const ULLMNPCPublicActionDefinition* Definition = FindDefinition(Pair.Key);
		if (!Definition)
		{
			continue;
		}
		FString SearchText = Definition->SelectionSummary;
		SearchText += TEXT(" ") + FString::Join(Definition->SuitableWhen, TEXT(" "));
		SearchText += TEXT(" ") + FString::Join(Definition->AvoidWhen, TEXT(" "));
		SearchText += TEXT(" ") + FString::Join(Definition->SearchKeywords, TEXT(" "));
		for (const FString& Token : Tokenize(SearchText))
		{
			DescriptionTokenIndex.FindOrAdd(Token).AddUnique(Pair.Key);
		}
	}

	TArray<FString> HashInputs;
	HashInputs.Add(Vocabulary->ContentHash);
	for (const TPair<FName, const ULLMNPCPublicActionDefinition*>& Pair : DefinitionIndex)
	{
		HashInputs.Add(Pair.Value->ContentHash);
	}
	for (const TPair<FName, const ULLMNPCMotionTemplate*>& Pair : TemplateIndex)
	{
		HashInputs.Add(Pair.Value->Metadata.CatalogContentHash);
	}
	HashInputs.Sort();
	CatalogHash = FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*FString::Join(HashInputs, TEXT("\n")))
	);
	return true;
}

void FLLMNPCTemplateSearchIndex::Reset()
{
	TemplateIndex.Reset();
	DefinitionIndex.Reset();
	PublicActionIndex.Reset();
	DescriptionTokenIndex.Reset();
	Diagnostics.Reset();
	CatalogHash.Reset();
}

const ULLMNPCMotionTemplate* FLLMNPCTemplateSearchIndex::FindTemplate(
	FName TemplateId
) const
{
	const ULLMNPCMotionTemplate* const* Found = TemplateIndex.Find(TemplateId);
	return Found ? *Found : nullptr;
}

const ULLMNPCPublicActionDefinition* FLLMNPCTemplateSearchIndex::FindDefinition(
	FName PublicActionId
) const
{
	const ULLMNPCPublicActionDefinition* const* Found =
		DefinitionIndex.Find(PublicActionId);
	return Found ? *Found : nullptr;
}

const TArray<FName>* FLLMNPCTemplateSearchIndex::FindVariants(
	FName PublicActionId
) const
{
	return PublicActionIndex.Find(PublicActionId);
}

void FLLMNPCTemplateSearchIndex::GetTemplateIds(
	TArray<FName>& OutTemplateIds
) const
{
	TemplateIndex.GetKeys(OutTemplateIds);
	OutTemplateIds.Sort(FNameLexicalLess());
}

void FLLMNPCTemplateSearchIndex::GetPublicActionIds(
	TArray<FName>& OutPublicActionIds
) const
{
	PublicActionIndex.GetKeys(OutPublicActionIds);
	OutPublicActionIds.Sort(FNameLexicalLess());
}

void FLLMNPCTemplateSearchIndex::QueryDescriptionTokens(
	const FString& Query,
	TArray<FName>& OutPublicActionIds
) const
{
	OutPublicActionIds.Reset();
	TMap<FName, int32> Scores;
	for (const FString& Token : Tokenize(Query))
	{
		if (const TArray<FName>* Matches = DescriptionTokenIndex.Find(Token))
		{
			for (const FName Match : *Matches)
			{
				++Scores.FindOrAdd(Match);
			}
		}
	}
	Scores.KeySort(
		[&Scores](FName A, FName B)
		{
			const int32 AScore = Scores.FindRef(A);
			const int32 BScore = Scores.FindRef(B);
			return AScore == BScore ? A.LexicalLess(B) : AScore > BScore;
		}
	);
	Scores.GetKeys(OutPublicActionIds);
}

bool FLLMNPCTemplateSearchIndex::BuildRuntimeCandidate(
	FName PublicActionId,
	FName SkeletonProfileId,
	FLLMNPCTemplateCandidate& OutCandidate
) const
{
	OutCandidate = FLLMNPCTemplateCandidate();
	const ULLMNPCPublicActionDefinition* Definition = FindDefinition(PublicActionId);
	const TArray<FName>* VariantIds = FindVariants(PublicActionId);
	if (!Definition || !VariantIds || SkeletonProfileId.IsNone())
	{
		return false;
	}

	OutCandidate.SelectionId = PublicActionId;
	OutCandidate.SelectionSummary = Definition->SelectionSummary;
	OutCandidate.Description = FText::FromString(Definition->SelectionSummary);
	OutCandidate.SuitableWhen = Definition->SuitableWhen;
	OutCandidate.AvoidWhen = Definition->AvoidWhen;
	OutCandidate.SemanticEffectTags = Definition->SemanticEffectTags;
	OutCandidate.TargetCategoryTags = Definition->TargetCategoryTags;
	OutCandidate.bRequiresTarget = Definition->bRequiresTarget;
	OutCandidate.RecommendedStyle = Definition->DefaultStyle;
	OutCandidate.DefinitionRevision = Definition->DefinitionRevision;
	OutCandidate.CatalogHash = CatalogHash;
	OutCandidate.CooldownSeconds = 0.0f;
	bool bHasCompatibleVariant = false;
	bool bAllMirrorAllowed = true;

	for (const FName VariantId : *VariantIds)
	{
		const ULLMNPCMotionTemplate* Variant = FindTemplate(VariantId);
		if (!Variant || !Variant->SupportsSkeletonProfile(SkeletonProfileId))
		{
			continue;
		}
		bHasCompatibleVariant = true;
		AddUniqueNames(OutCandidate.IntentTags, Variant->Metadata.IntentTags);
		AddUniqueNames(OutCandidate.EmotionTags, Variant->Metadata.EmotionTags);
		AddUniqueNames(OutCandidate.PersonalityTags, Variant->Metadata.PersonalityTags);
		AddUniqueNames(OutCandidate.BodyRegionTags, Variant->Metadata.BodyRegionTags);
		AddUniqueNames(OutCandidate.RequiredChannels, Variant->Metadata.RequiredChannels);
		AddUniqueNames(OutCandidate.BlockedStates, Variant->Metadata.BlockedStates);
		OutCandidate.CooldownSeconds = FMath::Max(
			OutCandidate.CooldownSeconds,
			Variant->Metadata.CooldownSeconds
		);
		bAllMirrorAllowed &= Variant->ModifierPolicy.bAllowMirror;

		TArray<FName> Styles = Variant->Metadata.VariantStyleTags;
		if (Styles.IsEmpty())
		{
			Styles = Variant->ModifierPolicy.AllowedStyleTags;
		}
		for (const FName Style : Styles)
		{
			if (Variant->ModifierPolicy.AllowedStyleTags.Contains(Style))
			{
				AddOrIntersectStyle(OutCandidate.StyleOptions, Style, *Variant);
			}
		}
	}
	if (!bHasCompatibleVariant || OutCandidate.StyleOptions.IsEmpty())
	{
		OutCandidate = FLLMNPCTemplateCandidate();
		return false;
	}

	OutCandidate.StyleOptions.Sort(
		[](const FLLMNPCCandidateStyleOption& A, const FLLMNPCCandidateStyleOption& B)
		{
			return A.Style.LexicalLess(B.Style);
		}
	);
	for (const FLLMNPCCandidateStyleOption& StyleOption : OutCandidate.StyleOptions)
	{
		OutCandidate.AllowedStyles.Add(StyleOption.Style);
	}
	const FLLMNPCCandidateStyleOption* DefaultOption =
		OutCandidate.StyleOptions.FindByPredicate(
			[&OutCandidate](const FLLMNPCCandidateStyleOption& Option)
			{
				return Option.Style == OutCandidate.RecommendedStyle;
			}
		);
	if (!DefaultOption)
	{
		DefaultOption = &OutCandidate.StyleOptions[0];
		OutCandidate.RecommendedStyle = DefaultOption->Style;
	}
	OutCandidate.AmplitudeRange = DefaultOption->AmplitudeRange;
	OutCandidate.SpeedRange = DefaultOption->SpeedRange;
	OutCandidate.DurationRange = DefaultOption->DurationRange;
	OutCandidate.RecommendedAmplitude = FMath::Clamp(
		1.0f,
		OutCandidate.AmplitudeRange.X,
		OutCandidate.AmplitudeRange.Y
	);
	OutCandidate.RecommendedSpeedScale = FMath::Clamp(
		1.0f,
		OutCandidate.SpeedRange.X,
		OutCandidate.SpeedRange.Y
	);
	OutCandidate.RecommendedDurationScale = FMath::Clamp(
		1.0f,
		OutCandidate.DurationRange.X,
		OutCandidate.DurationRange.Y
	);
	OutCandidate.bAllowMirror = bAllMirrorAllowed;
	return true;
}
