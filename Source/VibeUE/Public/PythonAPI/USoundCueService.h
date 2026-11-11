// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "USoundCueService.generated.h"

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
 * feeds into the cue's root (FirstNode). SoundCueEditorSubsystem.connect_nodes
 * means "child provides audio TO parent at input slot".
 *
 * Python Usage:
 *   import unreal
 *
 *   # Create cues and add/connect nodes natively: unreal.SoundCueEditorSubsystem.
 *   # Inspect nodes (indices for the index-based calls)
 *   nodes = unreal.SoundCueService.list_nodes("/Game/Audio/SC_Footstep")
 *   for n in nodes:
 *       print(f"[{n.node_index}] {n.node_class} — children: {n.child_indices}")
 *
 * @note All methods are static
 * @note Save with EditorToolset.AssetToolset.SaveAsset after editing
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
	// NODE INSPECTION
	// ============================================================================

	/**
	 * List all sound nodes in a SoundCue graph.
	 *
	 * Returns all USoundNode-derived nodes (excluding the invisible output node).
	 * Node indices in the returned array are the indices used by connect_nodes,
	 * set_root_node, set_wave_player_asset, etc.
	 *
	 * Call this after any structural change to get fresh indices.
	 *
	 * @param AssetPath - Full path to the SoundCue asset
	 * @return Array of node information, empty on failure
	 *
	 * Example:
	 *   nodes = unreal.SoundCueService.list_nodes("/Game/Audio/SC_Footstep")
	 *   for n in nodes:
	 *       root_flag = " [ROOT]" if n.is_root_node else ""
	 *       print(f"[{n.node_index}] {n.node_class}{root_flag} children={n.child_indices}")
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Audio|Inspection")
	static TArray<FSoundCueNodeInfo> ListNodes(const FString& AssetPath);

	/** Reposition a node in the graph editor (purely visual).
	 *  Call list_nodes() first to get the correct index. */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Audio|Nodes")
	static bool MoveNode(
		const FString& AssetPath,
		int32 NodeIndex,
		float PosX,
		float PosY);

	
	// ============================================================================
	// NODE PROPERTIES
	// ============================================================================

	/**
	 * Get a property value from a sound node as a string.
	 *
	 * Uses UE property reflection — works for scalar types (float, bool, int32, FString).
	 * For WavePlayer: "bLooping", "bIsRetriggerable"
	 * For Modulator:  "PitchMin", "PitchMax", "VolumeMin", "VolumeMax"
	 * For Delay:      "DelayMin", "DelayMax"
	 *
	 * @param AssetPath    - Full path to the SoundCue asset
	 * @param NodeIndex    - Index of the node (from list_nodes)
	 * @param PropertyName - Exact C++ property name (case-sensitive)
	 * @return Property value as string, or empty string on failure
	 *
	 * Example:
	 *   val = unreal.SoundCueService.get_node_property("/Game/Audio/SC_Footstep", 0, "PitchMin")
	 *   print(val)  # "0.95"
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Audio|NodeProperties")
	static FString GetNodeProperty(
		const FString& AssetPath,
		int32 NodeIndex,
		const FString& PropertyName);

	/**
	 * Set a property value on a sound node from a string.
	 *
	 * Uses UE property reflection — works for scalar types (float, bool, int32, FString).
	 * For WavePlayer: "bLooping" = "true"/"false"
	 * For Modulator:  "PitchMin" = "0.9", "VolumeMax" = "1.1"
	 * For Delay:      "DelayMin" = "0.1", "DelayMax" = "0.5"
	 *
	 * @param AssetPath    - Full path to the SoundCue asset
	 * @param NodeIndex    - Index of the node (from list_nodes)
	 * @param PropertyName - Exact C++ property name (case-sensitive)
	 * @param Value        - New value as string (e.g. "0.9", "true", "42")
	 * @return True if property was set successfully
	 *
	 * Example:
	 *   unreal.SoundCueService.set_node_property("/Game/Audio/SC_Footstep", 0, "PitchMin", "0.85")
	 *   unreal.SoundCueService.set_node_property("/Game/Audio/SC_Footstep", 0, "bLooping", "true")
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Audio|NodeProperties")
	static bool SetNodeProperty(
		const FString& AssetPath,
		int32 NodeIndex,
		const FString& PropertyName,
		const FString& Value);

	/**
	 * Assign a SoundWave to an existing Wave Player node.
	 *
	 * @param AssetPath     - Full path to the SoundCue asset
	 * @param NodeIndex     - Index of the WavePlayer node (from list_nodes)
	 * @param SoundWavePath - Full path to the SoundWave asset
	 * @return True if successful; False if node is not a WavePlayer or not found
	 *
	 * Example:
	 *   unreal.SoundCueService.set_wave_player_asset(
	 *       "/Game/Audio/SC_Footstep", 1, "/Game/Audio/SW_Step_Gravel")
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Audio|NodeProperties")
	static bool SetWavePlayerAsset(
		const FString& AssetPath,
		int32 NodeIndex,
		const FString& SoundWavePath);

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
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category = "VibeUE|Audio|SoundWave")
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
