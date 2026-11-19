// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "USoundCueService.generated.h"

/**
 * Information about a single node inside a SoundCue graph.
 * NodeIndex is stable within a single call session but should be refreshed via
 * SoundCueEditorSubsystem.describe_sound_cue() after any structural mutation.
 */
USTRUCT(BlueprintType)
struct FSoundCueNodeInfo
{
	GENERATED_BODY()

	/** 0-based index into the non-root node list returned by describe_sound_cue() */
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
 * Node indexing: SoundCueEditorSubsystem.describe_sound_cue() returns nodes in graph order (0-based).
 * Indices are valid until the next structural mutation, so always re-read them
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
 *   d = unreal.get_editor_subsystem(unreal.SoundCueEditorSubsystem).describe_sound_cue(unreal.load_asset("/Game/Audio/SC_Footstep"))
 *   for i, n in enumerate(d.nodes):
 *       print(f"[{i}] {n.title} — children: {n.children}")
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
