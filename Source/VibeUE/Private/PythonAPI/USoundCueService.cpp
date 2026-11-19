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

