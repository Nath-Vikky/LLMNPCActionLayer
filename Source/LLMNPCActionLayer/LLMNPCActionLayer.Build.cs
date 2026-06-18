// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LLMNPCActionLayer : ModuleRules
{
	public LLMNPCActionLayer(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"DeveloperSettings",
				"Engine",
				"AnimGraphRuntime",
				"AnimationCore"
			}
			);
			
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"HTTP",
				"Json",
				"JsonUtilities",
				"Projects"
			}
			);
	}
}
