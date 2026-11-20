// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "USoundCueService.generated.h"

// ============================================================================
// RESULT / INFO STRUCTS
// ============================================================================

/** Result returned by mutating SoundCue operations */
USTRUCT(BlueprintType)
struct FSoundCueResult
{
	GENERATED_BODY()

	/** Whether the operation succeeded */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	bool bSuccess = false;

	/** Human-readable result message */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	FString Message;

	/** Asset path of the affected asset (empty on failure) */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	FString AssetPath;
};

/**
 * Information about a single node inside a SoundCue graph.
 * NodeIndex is stable within a single call session but should be refreshed via
 * list_nodes() after any structural mutation.
 */
USTRUCT(BlueprintType)
struct FSoundCueNodeInfo
{
	GENERATED_BODY()

	/** 0-based index into the non-root node list returned by list_nodes() */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	int32 NodeIndex = -1;

	/** UClass name, e.g. "SoundNodeWavePlayer", "SoundNodeRandom" */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	FString NodeClass;

	/** Editor display title */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	FString NodeTitle;

	/**
	 * Indices of nodes that feed INTO this node (i.e. entries in ChildNodes).
	 * -1 entries indicate an unconnected input slot.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	TArray<int32> ChildIndices;

	/** X position in the SoundCue editor graph */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	float PosX = 0.0f;

	/** Y position in the SoundCue editor graph */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	float PosY = 0.0f;

	/** True if this node is currently set as FirstNode (the cue output) */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	bool bIsRootNode = false;
};

/** Summary information about a SoundCue asset */
USTRUCT(BlueprintType)
struct FSoundCueInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	float VolumeMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	float PitchMultiplier = 1.0f;

	/** Name of the assigned SoundClass, or empty if none */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	FString SoundClassName;

	/** Asset path of the assigned attenuation settings asset, or empty if none */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	FString AttenuationPath;

	/** Total playback duration in seconds (estimated by UE from the node graph) */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	float Duration = 0.0f;

	/** Total number of sound nodes in the graph (excludes the output node) */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	int32 NodeCount = 0;

	/** Index (in list_nodes) of the node set as FirstNode, or -1 if none */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	int32 RootNodeIndex = -1;
};

/** Summary information about a SoundWave asset */
USTRUCT(BlueprintType)
struct FSoundWaveInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	FString AssetPath;

	/** Total playback duration in seconds (0 if unknown/streaming) */
	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	float Duration = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	int32 SampleRate = 0;

	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	int32 NumChannels = 0;

	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	bool bLooping = false;

	UPROPERTY(BlueprintReadOnly, Category = "VibeUE|Audio")
	bool bStreaming = false;
};

// ============================================================================
// SERVICE CLASS
// ============================================================================

/**
 * SoundCue authoring service exposed directly to Python.
 *
 * Provides creation, inspection, and editing of SoundCue assets including
 * their node graphs, attenuation settings, and SoundWave references.
 *
 * Node indexing: list_nodes() returns nodes in graph order (0-based). Indices
 * are valid until the next structural mutation, so always re-call list_nodes()
 * after add/connect/remove operations before referencing indices again.
 *
 * Audio flow in SoundCue graphs: audio travels FROM leaf nodes TOWARD the root.
 * WavePlayer nodes produce audio, which feeds UP into Mixer/Random/etc., which
 * feeds into the cue's root (FirstNode). connect_nodes(parent, child, slot)
 * means "child provides audio TO parent at input slot".
 *
 * Python Usage:
 *   import unreal
 *
 *   # Create a simple cue with a single wave
 *   result = unreal.SoundCueService.create_sound_cue(
 *       "/Game/Audio/SC_Footstep",
 *       "/Game/Audio/SW_Step_Stone"
 *   )
 *
 *   # Inspect nodes
 *   nodes = unreal.SoundCueService.list_nodes("/Game/Audio/SC_Footstep")
 *   for n in nodes:
 *       print(f"[{n.node_index}] {n.node_class} — children: {n.child_indices}")
 *
 *   # Build a randomised footstep cue manually
 *   r = unreal.SoundCueService.add_random_node("/Game/Audio/SC_Footstep", -200, 0)
 *   wp1 = unreal.SoundCueService.add_wave_player_node(
 *       "/Game/Audio/SC_Footstep", "/Game/Audio/SW_Step_Stone", -400, -100)
 *   wp2 = unreal.SoundCueService.add_wave_player_node(
 *       "/Game/Audio/SC_Footstep", "/Game/Audio/SW_Step_Dirt", -400, 100)
 *   nodes = unreal.SoundCueService.list_nodes("/Game/Audio/SC_Footstep")
 *   # ... connect and set root
 *
 * @note All methods are static
 * @note Edits only dirty the package: save with Epic's EditorToolset.AssetToolset.SaveAsset
 *
 * Use Epic's engine toolsets for everything outside the cue's node graph:
 *  - save / duplicate / delete: `EditorToolset.AssetToolset` (SaveAsset / Duplicate / Delete)
 *  - import an audio file as a SoundWave: `EditorToolset.SoundWaveToolset.ImportFile`
 *  - cue settings (VolumeMultiplier, PitchMultiplier, SoundClassObject, AttenuationSettings,
 *    ConcurrencySet) and SoundWave properties (bLooping, Volume, Pitch, SubtitlePriority):
 *    `EditorToolset.ObjectToolset` (GetProperties / SetProperties)
 *
 * **C++ Source:**
 *
 * - **Plugin**: VibeUE
 * - **Module**: VibeUE
 * - **File**: USoundCueService.h
 */
UCLASS(BlueprintType)
class VIBEUE_API USoundCueService : public UToolsetDefinition
{
	GENERATED_BODY()

public:

	// ============================================================================
	// ASSET LIFECYCLE
	// ============================================================================



	// ============================================================================
	// NODE INSPECTION
	// ============================================================================


	// ============================================================================
	// NODE CREATION
	// ============================================================================















	/** Reposition a node in the graph editor (purely visual).
	 *  Call list_nodes() first to get the correct index. */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Audio|Nodes")
	static bool MoveNode(
		const FString& AssetPath,
		int32 NodeIndex,
		float PosX,
		float PosY);

	// ============================================================================
	// NODE CONNECTIONS
	// ============================================================================





	// ============================================================================
	// NODE PROPERTIES
	// ============================================================================




	// ============================================================================
	// SOUNDWAVE UTILITIES
	// ============================================================================

	/**
	 * Get information about a SoundWave asset.
	 *
	 * @param SoundWavePath - Full path to the SoundWave asset
	 * @return FSoundWaveInfo; AssetPath will be empty on failure
	 *
	 * Example:
	 *   info = unreal.SoundCueService.get_sound_wave_info("/Game/Audio/SW_Step_Stone")
	 *   print(f"Duration: {info.duration}s, Channels: {info.num_channels}")
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Audio|SoundWave")
	static FSoundWaveInfo GetSoundWaveInfo(const FString& SoundWavePath);

private:

	/** Load a SoundCue from a content-browser path. Returns nullptr on failure. */
	static class USoundCue* LoadSoundCue(const FString& AssetPath);

	/** Load a SoundWave from a content-browser path. Returns nullptr on failure. */
	static class USoundWave* LoadSoundWave(const FString& AssetPath);

	/**
	 * Collect all USoundCueGraphNode instances from the cue's graph, in order.
	 * Excludes the invisible USoundCueGraphNode_Root output node.
	 * The returned array index corresponds to the NodeIndex used throughout this service.
	 */
	static TArray<class USoundCueGraphNode*> GetGraphNodes(class USoundCue* SoundCue);

	/** Get a specific graph node by index. Returns nullptr if out of range. */
	static class USoundCueGraphNode* GetGraphNodeAtIndex(class USoundCue* SoundCue, int32 Index);
};
