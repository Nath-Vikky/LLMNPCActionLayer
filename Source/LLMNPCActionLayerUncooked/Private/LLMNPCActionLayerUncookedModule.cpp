#include "Modules/ModuleManager.h"

#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LLMProceduralPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_Root.h"
#include "Animation/AnimBlueprint.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

DEFINE_LOG_CATEGORY_STATIC(LogLLMNPCActionLayerUncooked, Log, All);

namespace
{
UEdGraphPin* FindPin(UEdGraphNode* Node, const FName PinName, const EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName == PinName && Pin->Direction == Direction)
		{
			return Pin;
		}
	}

	return nullptr;
}

bool CompileAndSaveAnimBlueprint(UAnimBlueprint* AnimBlueprint)
{
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);
	FKismetEditorUtilities::CompileBlueprint(AnimBlueprint);
	if (AnimBlueprint->Status == BS_Error)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_ANIMGRAPH_COMPILE_FAILED: %s"),
			*AnimBlueprint->GetPathName());
		return false;
	}

	AnimBlueprint->MarkPackageDirty();
	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(AnimBlueprint->GetOutermost());
	return UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
}

bool InstallMainAnimGraphNode(const FString& AnimBlueprintObjectPath)
{
	UAnimBlueprint* AnimBlueprint = LoadObject<UAnimBlueprint>(nullptr, *AnimBlueprintObjectPath);
	if (!AnimBlueprint)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_ANIMGRAPH_ASSET_NOT_FOUND: %s"),
			*AnimBlueprintObjectPath);
		return false;
	}

	TArray<UEdGraph*> Graphs;
	AnimBlueprint->GetAllGraphs(Graphs);
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		if (Graph && Graph->GetFName() == TEXT("AnimGraph"))
		{
			AnimGraph = Graph;
			break;
		}
	}

	if (!AnimGraph)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_ANIMGRAPH_NOT_FOUND: %s"),
			*AnimBlueprintObjectPath);
		return false;
	}

	TArray<UAnimGraphNode_LLMProceduralPose*> ExistingNodes;
	AnimGraph->GetNodesOfClass(ExistingNodes);
	if (ExistingNodes.Num() > 0)
	{
		ExistingNodes[0]->Node.bReadSnapshotFromMotionComponent = true;
		const bool bSaved = CompileAndSaveAnimBlueprint(AnimBlueprint);
		if (bSaved)
		{
			UE_LOG(
				LogLLMNPCActionLayerUncooked,
				Display,
				TEXT("LLMNPC_EDITOR_ANIMGRAPH_ALREADY_INSTALLED: %s"),
				*AnimBlueprintObjectPath);
		}
		else
		{
			UE_LOG(
				LogLLMNPCActionLayerUncooked,
				Error,
				TEXT("LLMNPC_EDITOR_ANIMGRAPH_SAVE_FAILED: %s"),
				*AnimBlueprintObjectPath);
		}
		return bSaved;
	}

	TArray<UAnimGraphNode_Root*> RootNodes;
	AnimGraph->GetNodesOfClass(RootNodes);
	if (RootNodes.Num() != 1)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_ANIMGRAPH_ROOT_COUNT_INVALID: %s (%d)"),
			*AnimBlueprintObjectPath,
			RootNodes.Num());
		return false;
	}

	UAnimGraphNode_Root* RootNode = RootNodes[0];
	UEdGraphPin* RootResultPin = FindPin(RootNode, TEXT("Result"), EGPD_Input);
	if (!RootResultPin || RootResultPin->LinkedTo.Num() != 1 || !RootResultPin->LinkedTo[0])
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_ANIMGRAPH_ROOT_SOURCE_INVALID: %s"),
			*AnimBlueprintObjectPath);
		return false;
	}

	UEdGraphPin* OriginalSourcePin = RootResultPin->LinkedTo[0];
	UEdGraphNode* OriginalSourceNode = OriginalSourcePin->GetOwningNode();
	const UEdGraphSchema* Schema = AnimGraph->GetSchema();
	if (!OriginalSourceNode || !Schema)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_ANIMGRAPH_SCHEMA_INVALID: %s"),
			*AnimBlueprintObjectPath);
		return false;
	}

	const FScopedTransaction Transaction(
		NSLOCTEXT(
			"LLMNPCActionLayer",
			"InstallMainAnimGraphNode",
			"Install LLM Procedural Pose In Main AnimGraph"));
	AnimBlueprint->Modify();
	AnimGraph->Modify();
	RootNode->Modify();
	OriginalSourceNode->Modify();

	FGraphNodeCreator<UAnimGraphNode_LocalToComponentSpace> LocalToComponentCreator(*AnimGraph);
	UAnimGraphNode_LocalToComponentSpace* LocalToComponentNode = LocalToComponentCreator.CreateNode(false);
	LocalToComponentCreator.Finalize();

	FGraphNodeCreator<UAnimGraphNode_LLMProceduralPose> ProceduralPoseCreator(*AnimGraph);
	UAnimGraphNode_LLMProceduralPose* ProceduralPoseNode = ProceduralPoseCreator.CreateNode(false);
	ProceduralPoseNode->Node.bReadSnapshotFromMotionComponent = true;
	ProceduralPoseCreator.Finalize();

	FGraphNodeCreator<UAnimGraphNode_ComponentToLocalSpace> ComponentToLocalCreator(*AnimGraph);
	UAnimGraphNode_ComponentToLocalSpace* ComponentToLocalNode = ComponentToLocalCreator.CreateNode(false);
	ComponentToLocalCreator.Finalize();

	const int32 FirstNodeX = OriginalSourceNode->NodePosX + 300;
	const int32 GraphY = RootNode->NodePosY;
	LocalToComponentNode->NodePosX = FirstNodeX;
	LocalToComponentNode->NodePosY = GraphY;
	ProceduralPoseNode->NodePosX = FirstNodeX + 300;
	ProceduralPoseNode->NodePosY = GraphY;
	ComponentToLocalNode->NodePosX = FirstNodeX + 600;
	ComponentToLocalNode->NodePosY = GraphY;
	RootNode->NodePosX = FMath::Max(RootNode->NodePosX, FirstNodeX + 900);

	UEdGraphPin* LocalPoseInput = FindPin(LocalToComponentNode, TEXT("LocalPose"), EGPD_Input);
	UEdGraphPin* ComponentPoseOutput = FindPin(LocalToComponentNode, TEXT("ComponentPose"), EGPD_Output);
	UEdGraphPin* ProceduralPoseInput = FindPin(ProceduralPoseNode, TEXT("ComponentPose"), EGPD_Input);
	UEdGraphPin* ProceduralPoseOutput = FindPin(ProceduralPoseNode, TEXT("Pose"), EGPD_Output);
	UEdGraphPin* ComponentPoseInput = FindPin(ComponentToLocalNode, TEXT("ComponentPose"), EGPD_Input);
	UEdGraphPin* LocalPoseOutput = FindPin(ComponentToLocalNode, TEXT("Pose"), EGPD_Output);

	const bool bPinsValid =
		LocalPoseInput &&
		ComponentPoseOutput &&
		ProceduralPoseInput &&
		ProceduralPoseOutput &&
		ComponentPoseInput &&
		LocalPoseOutput;
	const bool bConnectionsAllowed =
		bPinsValid &&
		Schema->CanCreateConnection(OriginalSourcePin, LocalPoseInput).Response != CONNECT_RESPONSE_DISALLOW &&
		Schema->CanCreateConnection(ComponentPoseOutput, ProceduralPoseInput).Response != CONNECT_RESPONSE_DISALLOW &&
		Schema->CanCreateConnection(ProceduralPoseOutput, ComponentPoseInput).Response != CONNECT_RESPONSE_DISALLOW &&
		Schema->CanCreateConnection(LocalPoseOutput, RootResultPin).Response != CONNECT_RESPONSE_DISALLOW;

	if (!bConnectionsAllowed)
	{
		AnimGraph->RemoveNode(LocalToComponentNode);
		AnimGraph->RemoveNode(ProceduralPoseNode);
		AnimGraph->RemoveNode(ComponentToLocalNode);
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_ANIMGRAPH_PIN_LAYOUT_INVALID: %s"),
			*AnimBlueprintObjectPath);
		return false;
	}

	OriginalSourcePin->BreakLinkTo(RootResultPin);
	const bool bConnected =
		Schema->TryCreateConnection(OriginalSourcePin, LocalPoseInput) &&
		Schema->TryCreateConnection(ComponentPoseOutput, ProceduralPoseInput) &&
		Schema->TryCreateConnection(ProceduralPoseOutput, ComponentPoseInput) &&
		Schema->TryCreateConnection(LocalPoseOutput, RootResultPin);

	if (!bConnected)
	{
		AnimGraph->RemoveNode(LocalToComponentNode);
		AnimGraph->RemoveNode(ProceduralPoseNode);
		AnimGraph->RemoveNode(ComponentToLocalNode);
		Schema->TryCreateConnection(OriginalSourcePin, RootResultPin);
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_ANIMGRAPH_CONNECTION_FAILED: %s"),
			*AnimBlueprintObjectPath);
		return false;
	}

	if (!CompileAndSaveAnimBlueprint(AnimBlueprint))
	{
		return false;
	}

	UE_LOG(
		LogLLMNPCActionLayerUncooked,
		Display,
		TEXT("LLMNPC_EDITOR_ANIMGRAPH_INSTALLED: %s"),
		*AnimBlueprintObjectPath);
	return true;
}

bool ConfigureCharacterMesh(
	const FString& BlueprintObjectPath,
	const FString& SkeletalMeshObjectPath)
{
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintObjectPath);
	USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *SkeletalMeshObjectPath);
	if (!Blueprint || !SkeletalMesh)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_CHARACTER_MESH_ASSET_NOT_FOUND: Blueprint=%s Mesh=%s"),
			*BlueprintObjectPath,
			*SkeletalMeshObjectPath);
		return false;
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_CHARACTER_BLUEPRINT_COMPILE_FAILED: %s"),
			*BlueprintObjectPath);
		return false;
	}

	ACharacter* CharacterDefault = Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject());
	USkeletalMeshComponent* MeshComponent = CharacterDefault ? CharacterDefault->GetMesh() : nullptr;
	if (!MeshComponent)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_CHARACTER_MESH_COMPONENT_NOT_FOUND: %s"),
			*BlueprintObjectPath);
		return false;
	}

	const FScopedTransaction Transaction(
		NSLOCTEXT(
			"LLMNPCActionLayer",
			"ConfigureCharacterMesh",
			"Configure Character Skeletal Mesh"));
	Blueprint->Modify();
	CharacterDefault->Modify();
	MeshComponent->Modify();
	MeshComponent->SetSkeletalMeshAsset(SkeletalMesh);
	MeshComponent->SetDisablePostProcessBlueprint(false);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	ACharacter* CompiledCharacterDefault =
		Blueprint->GeneratedClass
			? Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject())
			: nullptr;
	USkeletalMeshComponent* CompiledMeshComponent =
		CompiledCharacterDefault ? CompiledCharacterDefault->GetMesh() : nullptr;
	const bool bConfigured =
		Blueprint->Status != BS_Error &&
		CompiledMeshComponent &&
		CompiledMeshComponent->GetSkeletalMeshAsset() == SkeletalMesh &&
		!CompiledMeshComponent->GetDisablePostProcessBlueprint();
	if (!bConfigured)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_CHARACTER_MESH_VERIFY_FAILED: %s"),
			*BlueprintObjectPath);
		return false;
	}

	FBlueprintEditorUtils::PostEditChangeBlueprintActors(Blueprint, true);
	Blueprint->MarkPackageDirty();
	TArray<UPackage*> PackagesToSave;
	PackagesToSave.Add(Blueprint->GetOutermost());
	const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
	if (bSaved)
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Display,
			TEXT("LLMNPC_EDITOR_CHARACTER_MESH_CONFIGURED: Blueprint=%s Mesh=%s"),
			*BlueprintObjectPath,
			*SkeletalMeshObjectPath);
	}
	else
	{
		UE_LOG(
			LogLLMNPCActionLayerUncooked,
			Error,
			TEXT("LLMNPC_EDITOR_CHARACTER_MESH_SAVE_FAILED: %s"),
			*BlueprintObjectPath);
	}
	return bSaved;
}
}

class FLLMNPCActionLayerUncookedModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		InstallMainAnimGraphNodeCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("LLMNPC.Editor.InstallMainAnimGraphNode"),
			TEXT("Installs the self-driven LLM procedural pose chain before an Anim Blueprint output node."),
			FConsoleCommandWithArgsDelegate::CreateRaw(
				this,
				&FLLMNPCActionLayerUncookedModule::HandleInstallMainAnimGraphNode),
			ECVF_Default);

		ConfigureCharacterMeshCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("LLMNPC.Editor.ConfigureCharacterMesh"),
			TEXT("Sets and verifies the inherited skeletal mesh default on a Character Blueprint."),
			FConsoleCommandWithArgsDelegate::CreateRaw(
				this,
				&FLLMNPCActionLayerUncookedModule::HandleConfigureCharacterMesh),
			ECVF_Default);
	}

	virtual void ShutdownModule() override
	{
		if (InstallMainAnimGraphNodeCommand)
		{
			IConsoleManager::Get().UnregisterConsoleObject(InstallMainAnimGraphNodeCommand);
			InstallMainAnimGraphNodeCommand = nullptr;
		}
		if (ConfigureCharacterMeshCommand)
		{
			IConsoleManager::Get().UnregisterConsoleObject(ConfigureCharacterMeshCommand);
			ConfigureCharacterMeshCommand = nullptr;
		}
	}

private:
	void HandleInstallMainAnimGraphNode(const TArray<FString>& Args)
	{
		if (Args.Num() != 1)
		{
			UE_LOG(
				LogLLMNPCActionLayerUncooked,
				Error,
				TEXT("Usage: LLMNPC.Editor.InstallMainAnimGraphNode /Game/Path/ABP_Name.ABP_Name"));
			return;
		}

		InstallMainAnimGraphNode(Args[0]);
	}

	void HandleConfigureCharacterMesh(const TArray<FString>& Args)
	{
		if (Args.Num() != 2)
		{
			UE_LOG(
				LogLLMNPCActionLayerUncooked,
				Error,
				TEXT(
					"Usage: LLMNPC.Editor.ConfigureCharacterMesh "
					"/Game/Path/BP_Character.BP_Character "
					"/Game/Path/SKM_Character.SKM_Character"));
			return;
		}

		ConfigureCharacterMesh(Args[0], Args[1]);
	}

	IConsoleObject* InstallMainAnimGraphNodeCommand = nullptr;
	IConsoleObject* ConfigureCharacterMeshCommand = nullptr;
};

IMPLEMENT_MODULE(FLLMNPCActionLayerUncookedModule, LLMNPCActionLayerUncooked)
