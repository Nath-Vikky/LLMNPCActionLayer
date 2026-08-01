#pragma once

#include "CoreMinimal.h"

namespace LLMNPCMotionRecipe
{
inline constexpr const TCHAR* SchemaVersion = TEXT("llmnpc.motion_recipe.v1");
inline constexpr const TCHAR* RegistryVersion = TEXT("llmnpc.motion_primitives.v4");
inline constexpr const TCHAR* CompilerVersion = TEXT("llmnpc.motion_recipe_compiler.v6");
inline constexpr int32 DefaultMaxPrimitiveCount = 12;
inline constexpr int32 DefaultMaxTargetCount = 1;
inline constexpr int32 DefaultMaxActiveBodyRegions = 3;
inline constexpr float DefaultMaxDurationSeconds = 4.0f;
}

enum class ELLMNPCMotionRecipeValueType : uint8
{
	Number,
	String,
	Boolean
};

enum class ELLMNPCMotionPrimitiveAvailability : uint8
{
	RuntimeSafe,
	AuthoringOnly,
	Disabled
};

enum class ELLMNPCMotionPrimitiveOverlapPolicy : uint8
{
	Exclusive,
	Blend
};

enum class ELLMNPCMotionRecipeMode : uint8
{
	PublishedRuntime,
	AuthoringSandbox
};

struct LLMNPCACTIONLAYER_API FLLMNPCMotionRecipeValue
{
	ELLMNPCMotionRecipeValueType Type = ELLMNPCMotionRecipeValueType::Number;
	double NumberValue = 0.0;
	FString StringValue;
	bool bBooleanValue = false;

	static FLLMNPCMotionRecipeValue Number(double Value);
	static FLLMNPCMotionRecipeValue String(const FString& Value);
	static FLLMNPCMotionRecipeValue Boolean(bool bValue);
};

struct LLMNPCACTIONLAYER_API FLLMNPCMotionRecipePrimitive
{
	FName PrimitiveId = NAME_None;
	FName Side = TEXT("none");
	double StartTimeSeconds = 0.0;
	double EndTimeSeconds = 0.0;
	FName TargetSlot = NAME_None;
	TMap<FName, FLLMNPCMotionRecipeValue> Parameters;
	int32 SourceIndex = INDEX_NONE;

	const FLLMNPCMotionRecipeValue* FindParameter(FName ParameterId) const;
	double GetNumberParameter(FName ParameterId, double DefaultValue) const;
	FString GetStringParameter(FName ParameterId, const FString& DefaultValue) const;
	bool GetBooleanParameter(FName ParameterId, bool bDefaultValue) const;
};

struct LLMNPCACTIONLAYER_API FLLMNPCMotionRecipe
{
	FString SchemaVersion = LLMNPCMotionRecipe::SchemaVersion;
	FString RecipeId;
	FString Intent;
	double DurationSeconds = 0.0;
	bool bInterruptible = true;
	TArray<FLLMNPCMotionRecipePrimitive> Primitives;
};

struct LLMNPCACTIONLAYER_API FLLMNPCMotionPrimitiveParameterSchema
{
	FName ParameterId = NAME_None;
	ELLMNPCMotionRecipeValueType ValueType = ELLMNPCMotionRecipeValueType::Number;
	FName Unit = NAME_None;
	bool bRequired = false;
	bool bInteger = false;
	bool bHasDefault = false;
	FLLMNPCMotionRecipeValue DefaultValue;
	double MinValue = 0.0;
	double MaxValue = 1.0;
	TArray<FString> AllowedStringValues;
	FString Description;
};

struct LLMNPCACTIONLAYER_API FLLMNPCMotionPrimitiveDefinition
{
	FName PrimitiveId = NAME_None;
	FString SchemaVersion = LLMNPCMotionRecipe::SchemaVersion;
	ELLMNPCMotionPrimitiveAvailability Availability =
		ELLMNPCMotionPrimitiveAvailability::AuthoringOnly;
	FName BodyRegion = NAME_None;
	TArray<FName> AllowedSides;
	TArray<FName> AllowedTargetModes;
	bool bTargetRequired = false;
	TArray<FLLMNPCMotionPrimitiveParameterSchema> ParameterSchemas;
	TArray<FName> RequiredCapabilities;
	TArray<FName> RequiredChannelPatterns;
	TArray<FName> BlockedStates;
	FName MirroringPolicy = TEXT("none");
	ELLMNPCMotionPrimitiveOverlapPolicy OverlapPolicy =
		ELLMNPCMotionPrimitiveOverlapPolicy::Exclusive;
	FName SolverId = NAME_None;
	double MinDurationSeconds = 0.05;
	double MaxDurationSeconds = 4.0;
	int32 MaxInstancesPerRecipe = 1;
	FString Description;

	const FLLMNPCMotionPrimitiveParameterSchema* FindParameterSchema(
		FName ParameterId
	) const;
};

struct LLMNPCACTIONLAYER_API FLLMNPCMotionRecipeValidationContext
{
	ELLMNPCMotionRecipeMode Mode = ELLMNPCMotionRecipeMode::AuthoringSandbox;
	TSet<FName> AllowedTargetSlots;
	TSet<FName> ActiveBlockedStates;
	float MaxDurationSeconds = LLMNPCMotionRecipe::DefaultMaxDurationSeconds;
	int32 MaxPrimitiveCount = LLMNPCMotionRecipe::DefaultMaxPrimitiveCount;
	int32 MaxTargetCount = LLMNPCMotionRecipe::DefaultMaxTargetCount;
	int32 MaxActiveBodyRegions =
		LLMNPCMotionRecipe::DefaultMaxActiveBodyRegions;
};

struct LLMNPCACTIONLAYER_API FLLMNPCMotionRecipeValidationResult
{
	bool bValid = false;
	FString ErrorCode;
	int32 PrimitiveIndex = INDEX_NONE;
	TArray<FString> NormalizationRecords;
	TArray<FName> RequiredChannels;
	TArray<FName> UsedTargetSlots;
	TArray<FName> ActiveBodyRegions;
};

struct LLMNPCACTIONLAYER_API FLLMNPCCompiledPrimitiveMetadata
{
	int32 SourceIndex = INDEX_NONE;
	FName PrimitiveId = NAME_None;
	FName SolverId = NAME_None;
	TArray<FName> GeneratedControlIds;
	TArray<FName> RequiredChannels;
};

struct LLMNPCACTIONLAYER_API FLLMNPCCompiledRecipeMetadata
{
	FString RecipeHash;
	FString CapabilityHash;
	FString PrimitiveRegistryVersion = LLMNPCMotionRecipe::RegistryVersion;
	FString CompilerVersion = LLMNPCMotionRecipe::CompilerVersion;
	FString CompiledRecipeHash;
	TArray<FLLMNPCCompiledPrimitiveMetadata> PrimitiveMappings;
	TArray<FString> RewriteRecords;
	TMap<FName, FString> DynamicTargetBindings;
};

class LLMNPCACTIONLAYER_API FLLMNPCMotionRecipeCanonicalizer
{
public:
	static bool BuildCanonicalJson(
		const FLLMNPCMotionRecipe& Recipe,
		FString& OutJson,
		FString& OutError
	);

	static FString BuildRecipeHash(const FString& CanonicalRecipeJson);
};
