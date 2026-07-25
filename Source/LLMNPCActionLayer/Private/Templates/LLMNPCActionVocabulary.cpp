#include "Templates/LLMNPCActionVocabulary.h"

#include "Misc/SecureHash.h"

namespace
{
FString NormalizeName(FName Name)
{
	return Name.ToString().TrimStartAndEnd().ToLower();
}

FString FieldToString(ELLMNPCActionVocabularyField Field)
{
	return StaticEnum<ELLMNPCActionVocabularyField>()->GetNameStringByValue(
		static_cast<int64>(Field)
	);
}
}

bool ULLMNPCActionVocabulary::ValidateVocabulary(FString& OutError) const
{
	OutError.Reset();
	if (
		VocabularyId.IsNone() ||
		SchemaVersion != LLMNPCCatalog::VocabularySchemaVersion ||
		SemanticVersion.TrimStartAndEnd().IsEmpty() ||
		Revision < 1 ||
		Entries.IsEmpty()
	)
	{
		OutError = TEXT("LLMNPC_VOCABULARY_HEADER_INVALID");
		return false;
	}

	TSet<FName> Tags;
	TSet<FName> Aliases;
	for (const FLLMNPCActionVocabularyEntry& Entry : Entries)
	{
		if (
			Entry.Tag.IsNone() ||
			Entry.AllowedFields.IsEmpty() ||
			Tags.Contains(Entry.Tag) ||
			Aliases.Contains(Entry.Tag)
		)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_VOCABULARY_ENTRY_INVALID:%s"),
				*Entry.Tag.ToString()
			);
			return false;
		}
		Tags.Add(Entry.Tag);

		TSet<ELLMNPCActionVocabularyField> UniqueFields;
		for (const ELLMNPCActionVocabularyField Field : Entry.AllowedFields)
		{
			if (UniqueFields.Contains(Field))
			{
				OutError = FString::Printf(
					TEXT("LLMNPC_VOCABULARY_FIELD_DUPLICATE:%s:%s"),
					*Entry.Tag.ToString(),
					*FieldToString(Field)
				);
				return false;
			}
			UniqueFields.Add(Field);
		}

		for (const FName Synonym : Entry.Synonyms)
		{
			if (
				Synonym.IsNone() ||
				Synonym == Entry.Tag ||
				Tags.Contains(Synonym) ||
				Aliases.Contains(Synonym)
			)
			{
				OutError = FString::Printf(
					TEXT("LLMNPC_VOCABULARY_SYNONYM_INVALID:%s:%s"),
					*Entry.Tag.ToString(),
					*Synonym.ToString()
				);
				return false;
			}
			Aliases.Add(Synonym);
		}
		if (Entry.bDeprecated && Entry.ReplacementTag.IsNone())
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_VOCABULARY_REPLACEMENT_MISSING:%s"),
				*Entry.Tag.ToString()
			);
			return false;
		}
	}

	for (const FLLMNPCActionVocabularyEntry& Entry : Entries)
	{
		if (
			Entry.bDeprecated &&
			(!Tags.Contains(Entry.ReplacementTag) || Entry.ReplacementTag == Entry.Tag)
		)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_VOCABULARY_REPLACEMENT_INVALID:%s"),
				*Entry.Tag.ToString()
			);
			return false;
		}
	}

	if (!ContentHash.IsEmpty() && ContentHash != BuildContentHash(*this))
	{
		OutError = TEXT("LLMNPC_VOCABULARY_CONTENT_HASH_STALE");
		return false;
	}
	return true;
}

bool ULLMNPCActionVocabulary::IsTagAllowed(
	FName Tag,
	ELLMNPCActionVocabularyField Field,
	bool bAllowDeprecated
) const
{
	const FName Resolved = ResolveTag(Tag);
	const FLLMNPCActionVocabularyEntry* Entry = Entries.FindByPredicate(
		[Resolved](const FLLMNPCActionVocabularyEntry& Candidate)
		{
			return Candidate.Tag == Resolved;
		}
	);
	return
		Entry &&
		(bAllowDeprecated || !Entry->bDeprecated) &&
		Entry->AllowedFields.Contains(Field);
}

FName ULLMNPCActionVocabulary::ResolveTag(FName TagOrSynonym) const
{
	if (TagOrSynonym.IsNone())
	{
		return NAME_None;
	}
	const FString Normalized = NormalizeName(TagOrSynonym);
	for (const FLLMNPCActionVocabularyEntry& Entry : Entries)
	{
		if (NormalizeName(Entry.Tag) == Normalized)
		{
			return Entry.bDeprecated ? Entry.ReplacementTag : Entry.Tag;
		}
		for (const FName Synonym : Entry.Synonyms)
		{
			if (NormalizeName(Synonym) == Normalized)
			{
				return Entry.bDeprecated ? Entry.ReplacementTag : Entry.Tag;
			}
		}
	}
	return NAME_None;
}

bool ULLMNPCActionVocabulary::ValidateTags(
	const TArray<FName>& Tags,
	ELLMNPCActionVocabularyField Field,
	FString& OutError
) const
{
	OutError.Reset();
	TSet<FName> Unique;
	for (const FName Tag : Tags)
	{
		const FName Resolved = ResolveTag(Tag);
		if (
			Resolved.IsNone() ||
			Resolved != Tag ||
			Unique.Contains(Resolved) ||
			!IsTagAllowed(Resolved, Field)
		)
		{
			OutError = FString::Printf(
				TEXT("LLMNPC_VOCABULARY_TAG_INVALID:%s:%s"),
				*FieldToString(Field),
				*Tag.ToString()
			);
			return false;
		}
		Unique.Add(Resolved);
	}
	return true;
}

FString ULLMNPCActionVocabulary::BuildContentHash(
	const ULLMNPCActionVocabulary& Vocabulary
)
{
	TArray<FString> Lines;
	Lines.Add(FString::Printf(
		TEXT("%s|%s|%s|%d"),
		*Vocabulary.VocabularyId.ToString(),
		*Vocabulary.SchemaVersion,
		*Vocabulary.SemanticVersion,
		Vocabulary.Revision
	));
	for (const FLLMNPCActionVocabularyEntry& Entry : Vocabulary.Entries)
	{
		TArray<FString> Synonyms;
		for (const FName Synonym : Entry.Synonyms)
		{
			Synonyms.Add(NormalizeName(Synonym));
		}
		Synonyms.Sort();
		TArray<FString> Fields;
		for (const ELLMNPCActionVocabularyField Field : Entry.AllowedFields)
		{
			Fields.Add(FieldToString(Field));
		}
		Fields.Sort();
		Lines.Add(FString::Printf(
			TEXT("%s|%s|%s|%s|%s|%d|%s"),
			*NormalizeName(Entry.Tag),
			*Entry.EnglishDisplayName.ToString().TrimStartAndEnd(),
			*Entry.ChineseDisplayName.ToString().TrimStartAndEnd(),
			*FString::Join(Synonyms, TEXT(",")),
			*FString::Join(Fields, TEXT(",")),
			Entry.bDeprecated ? 1 : 0,
			*NormalizeName(Entry.ReplacementTag)
		));
	}
	Lines.Sort();
	return FString::Printf(
		TEXT("md5:%s"),
		*FMD5::HashAnsiString(*FString::Join(Lines, TEXT("\n")))
	);
}

FPrimaryAssetId ULLMNPCActionVocabulary::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("LLMNPCActionVocabulary"), GetFName());
}
