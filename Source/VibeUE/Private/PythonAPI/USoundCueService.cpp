// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/USoundCueService.h"

// SoundCue.h declares an override of the deprecated
// Audio::ILegacyParameterTransmitter::GetReferencedObjects, which trips C4996
// (promoted to C2220 by this module's bWarningsAsErrors) purely from including
// the header — VibeUE never calls the deprecated API itself. See GitHub #491.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
#include "Sound/SoundCue.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include "Sound/SoundWave.h"
#include "Sound/SoundNode.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeAttenuation.h"
#include "Sound/SoundNodeLooping.h"
#include "Sound/SoundNodeConcatenator.h"
#include "Sound/SoundNodeDelay.h"
#include "Sound/SoundNodeSwitch.h"
#include "Sound/SoundNodeEnveloper.h"
#include "Sound/SoundNodeDistanceCrossFade.h"
#include "Sound/SoundNodeBranch.h"
#include "Sound/SoundNodeParamCrossFade.h"
#include "Sound/SoundNodeQualityLevel.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "SoundCueGraph/SoundCueGraph.h"
#include "SoundCueGraph/SoundCueGraphNode.h"
#include "SoundCueGraph/SoundCueGraphNode_Root.h"
#include "EditorAssetLibrary.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/SoundCueFactoryNew.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetImportTask.h"

DEFINE_LOG_CATEGORY_STATIC(LogSoundCueService, Log, All);

// ============================================================================
// PRIVATE HELPERS
// ============================================================================

USoundCue* USoundCueService::LoadSoundCue(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogSoundCueService, Warning, TEXT("LoadSoundCue: path is empty"));
		return nullptr;
	}

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Loaded)
	{
		UE_LOG(LogSoundCueService, Warning, TEXT("LoadSoundCue: failed to load '%s'"), *AssetPath);
		return nullptr;
	}

	USoundCue* SoundCue = Cast<USoundCue>(Loaded);
	if (!SoundCue)
	{
		UE_LOG(LogSoundCueService, Warning, TEXT("LoadSoundCue: '%s' is not a SoundCue"), *AssetPath);
		return nullptr;
	}

	return SoundCue;
}

USoundWave* USoundCueService::LoadSoundWave(const FString& AssetPath)
{
	if (AssetPath.IsEmpty())
	{
		return nullptr;
	}

	UObject* Loaded = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Loaded)
	{
		UE_LOG(LogSoundCueService, Warning, TEXT("LoadSoundWave: failed to load '%s'"), *AssetPath);
		return nullptr;
	}

	USoundWave* Wave = Cast<USoundWave>(Loaded);
	if (!Wave)
	{
		UE_LOG(LogSoundCueService, Warning, TEXT("LoadSoundWave: '%s' is not a SoundWave"), *AssetPath);
		return nullptr;
	}

	return Wave;
}

TArray<USoundCueGraphNode*> USoundCueService::GetGraphNodes(USoundCue* SoundCue)
{
	TArray<USoundCueGraphNode*> Result;

	if (!SoundCue)
	{
		return Result;
	}

	UEdGraph* Graph = SoundCue->GetGraph();
	if (!Graph)
	{
		return Result;
	}

	for (UEdGraphNode* EdNode : Graph->Nodes)
	{
		// Cast<USoundCueGraphNode> returns nullptr for USoundCueGraphNode_Root
		// since Root extends USoundCueGraphNode_Base (a sibling, not a subclass of USoundCueGraphNode)
		USoundCueGraphNode* CueNode = Cast<USoundCueGraphNode>(EdNode);
		if (CueNode && CueNode->SoundNode)
		{
			Result.Add(CueNode);
		}
	}

	return Result;
}

USoundCueGraphNode* USoundCueService::GetGraphNodeAtIndex(USoundCue* SoundCue, int32 Index)
{
	TArray<USoundCueGraphNode*> Nodes = GetGraphNodes(SoundCue);
	if (!Nodes.IsValidIndex(Index))
	{
		return nullptr;
	}
	return Nodes[Index];
}

// ============================================================================
// NODE INSPECTION
// ============================================================================

TArray<FSoundCueNodeInfo> USoundCueService::ListNodes(const FString& AssetPath)
{
	TArray<FSoundCueNodeInfo> Result;

	USoundCue* SoundCue = LoadSoundCue(AssetPath);
	if (!SoundCue)
	{
		return Result;
	}

	TArray<USoundCueGraphNode*> Nodes = GetGraphNodes(SoundCue);

	for (int32 i = 0; i < Nodes.Num(); ++i)
	{
		USoundCueGraphNode* GraphNode = Nodes[i];
		USoundNode*         SoundNode = GraphNode->SoundNode;

		FSoundCueNodeInfo Info;
		Info.NodeIndex   = i;
		Info.NodeClass   = SoundNode->GetClass()->GetName();
		Info.NodeTitle   = SoundNode->GetTitle().ToString();
		Info.PosX        = (float)GraphNode->NodePosX;
		Info.PosY        = (float)GraphNode->NodePosY;
		Info.bIsRootNode = (SoundNode == SoundCue->FirstNode);

		// Resolve ChildNodes (inputs feeding INTO this node) to their list indices
		for (USoundNode* Child : SoundNode->ChildNodes)
		{
			int32 ChildIdx = -1;
			if (Child)
			{
				for (int32 j = 0; j < Nodes.Num(); ++j)
				{
					if (Nodes[j]->SoundNode == Child)
					{
						ChildIdx = j;
						break;
					}
				}
			}
			Info.ChildIndices.Add(ChildIdx);
		}

		Result.Add(Info);
	}

	return Result;
}

// Shared helper: add N input pins to a freshly-constructed graph node, then compile.
// Used by all multi-input nodes (Switch, DistanceCrossFade, ParamCrossFade).
static void AddPinsAndCompile(USoundCue* SoundCue, USoundCueGraphNode* GraphNode, int32 NumPins)
{
	for (int32 i = 0; i < NumPins; i++)
	{
		GraphNode->CreateInputPin();
	}
	SoundCue->CompileSoundNodesFromGraphNodes();
}

bool USoundCueService::MoveNode(const FString& AssetPath, int32 NodeIndex, float PosX, float PosY)
{
	USoundCue* SoundCue = LoadSoundCue(AssetPath);
	if (!SoundCue) { return false; }

	USoundCueGraphNode* GraphNode = GetGraphNodeAtIndex(SoundCue, NodeIndex);
	if (!GraphNode)
	{
		UE_LOG(LogSoundCueService, Warning, TEXT("MoveNode: NodeIndex %d out of range"), NodeIndex);
		return false;
	}

	GraphNode->NodePosX = (int32)PosX;
	GraphNode->NodePosY = (int32)PosY;
	SoundCue->MarkPackageDirty();
	return true;
}

// ============================================================================
// NODE PROPERTIES
// ============================================================================

bool USoundCueService::SetWavePlayerAsset(
	const FString& AssetPath, int32 NodeIndex, const FString& SoundWavePath)
{
	USoundCue* SoundCue = LoadSoundCue(AssetPath);
	if (!SoundCue)
	{
		return false;
	}

	USoundCueGraphNode* GraphNode = GetGraphNodeAtIndex(SoundCue, NodeIndex);
	if (!GraphNode)
	{
		UE_LOG(LogSoundCueService, Warning, TEXT("SetWavePlayerAsset: NodeIndex %d is out of range"), NodeIndex);
		return false;
	}

	USoundNodeWavePlayer* WavePlayer = Cast<USoundNodeWavePlayer>(GraphNode->SoundNode);
	if (!WavePlayer)
	{
		UE_LOG(LogSoundCueService, Warning,
			TEXT("SetWavePlayerAsset: Node %d is '%s', not a WavePlayer"),
			NodeIndex, *GraphNode->SoundNode->GetClass()->GetName());
		return false;
	}

	USoundWave* SoundWave = LoadSoundWave(SoundWavePath);
	if (!SoundWave)
	{
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("SoundCueService", "SetWavePlayerAsset", "Set WavePlayer Asset"));
	SoundCue->Modify();
	WavePlayer->Modify();

	WavePlayer->SetSoundWave(SoundWave);
	SoundCue->MarkPackageDirty();

	return true;
}

FString USoundCueService::GetNodeProperty(
	const FString& AssetPath, int32 NodeIndex, const FString& PropertyName)
{
	USoundCue* SoundCue = LoadSoundCue(AssetPath);
	if (!SoundCue)
	{
		return FString();
	}

	USoundCueGraphNode* GraphNode = GetGraphNodeAtIndex(SoundCue, NodeIndex);
	if (!GraphNode)
	{
		UE_LOG(LogSoundCueService, Warning, TEXT("GetNodeProperty: NodeIndex %d out of range"), NodeIndex);
		return FString();
	}

	USoundNode* SoundNode = GraphNode->SoundNode;
	FProperty* Prop = SoundNode->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		UE_LOG(LogSoundCueService, Warning,
			TEXT("GetNodeProperty: property '%s' not found on %s"),
			*PropertyName, *SoundNode->GetClass()->GetName());
		return FString();
	}

	FString ExportedValue;
	Prop->ExportText_InContainer(0, ExportedValue, SoundNode, SoundNode, SoundNode, PPF_None);
	return ExportedValue;
}

bool USoundCueService::SetNodeProperty(
	const FString& AssetPath, int32 NodeIndex,
	const FString& PropertyName, const FString& Value)
{
	USoundCue* SoundCue = LoadSoundCue(AssetPath);
	if (!SoundCue)
	{
		return false;
	}

	USoundCueGraphNode* GraphNode = GetGraphNodeAtIndex(SoundCue, NodeIndex);
	if (!GraphNode)
	{
		UE_LOG(LogSoundCueService, Warning, TEXT("SetNodeProperty: NodeIndex %d out of range"), NodeIndex);
		return false;
	}

	USoundNode* SoundNode = GraphNode->SoundNode;
	FProperty* Prop = SoundNode->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		UE_LOG(LogSoundCueService, Warning,
			TEXT("SetNodeProperty: property '%s' not found on %s"),
			*PropertyName, *SoundNode->GetClass()->GetName());
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("SoundCueService", "SetNodeProperty", "Set SoundNode Property"));
	SoundCue->Modify();
	SoundNode->Modify();

	const TCHAR* ImportResult = Prop->ImportText_InContainer(*Value, SoundNode, SoundNode, PPF_None);
	if (!ImportResult)
	{
		UE_LOG(LogSoundCueService, Warning,
			TEXT("SetNodeProperty: failed to set '%s' = '%s' on %s"),
			*PropertyName, *Value, *SoundNode->GetClass()->GetName());
		return false;
	}

	SoundNode->PostEditChange();
	SoundCue->MarkPackageDirty();
	return true;
}

// ============================================================================
// SOUNDWAVE UTILITIES
// ============================================================================

FSoundWaveInfo USoundCueService::GetSoundWaveInfo(const FString& SoundWavePath)
{
	FSoundWaveInfo Info;

	USoundWave* Wave = LoadSoundWave(SoundWavePath);
	if (!Wave)
	{
		return Info;
	}

	Info.AssetPath   = Wave->GetPathName();
	Info.Duration    = Wave->GetDuration();
	Info.SampleRate  = Wave->GetSampleRateForCurrentPlatform();
	Info.NumChannels = Wave->NumChannels;
	Info.bLooping    = Wave->bLooping;
	Info.bStreaming  = Wave->IsStreaming(nullptr);

	return Info;
}

bool USoundCueService::SetSoundWaveProperty(
	const FString& SoundWavePath, const FString& PropertyName, const FString& Value)
{
	USoundWave* Wave = LoadSoundWave(SoundWavePath);
	if (!Wave)
	{
		UE_LOG(LogSoundCueService, Warning,
			TEXT("SetSoundWaveProperty: could not load '%s'"), *SoundWavePath);
		return false;
	}

	FProperty* Prop = Wave->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		UE_LOG(LogSoundCueService, Warning,
			TEXT("SetSoundWaveProperty: property '%s' not found on USoundWave"), *PropertyName);
		return false;
	}

	FScopedTransaction Transaction(NSLOCTEXT("SoundCueService", "SetSoundWaveProp", "Set SoundWave Property"));
	Wave->Modify();

	const TCHAR* ImportResult = Prop->ImportText_InContainer(*Value, Wave, Wave, PPF_None);
	if (!ImportResult)
	{
		UE_LOG(LogSoundCueService, Warning,
			TEXT("SetSoundWaveProperty: failed to set '%s' = '%s'"), *PropertyName, *Value);
		return false;
	}

	Wave->PostEditChange();
	Wave->MarkPackageDirty();
	return true;
}
