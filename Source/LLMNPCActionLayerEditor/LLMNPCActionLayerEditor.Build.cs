using UnrealBuildTool;

public class LLMNPCActionLayerEditor : ModuleRules
{
	public LLMNPCActionLayerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"LLMNPCActionLayer"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"AssetTools",
				"DesktopPlatform",
				"EditorSubsystem",
				"HTTP",
				"InputCore",
				"Json",
				"JsonUtilities",
				"LevelEditor",
				"PropertyEditor",
				"Projects",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd"
			}
		);
	}
}
