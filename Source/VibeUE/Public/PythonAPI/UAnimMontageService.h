// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UAnimMontageService.generated.h"

// ============================================================================
// DATA TRANSFER OBJECTS (DTOs)
// ============================================================================

/**
 * Comprehensive information about an Animation Montage asset
 */
USTRUCT(BlueprintType)
struct FMontageInfo
{
	GENERATED_BODY()

	/** Asset path of the montage */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString MontagePath;

	/** Display name */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString MontageName;

	/** Associated skeleton path */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString SkeletonPath;

	/** Total duration in seconds */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float Duration = 0.0f;

	/** Number of sections */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 SectionCount = 0;

	/** Number of slot tracks */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 SlotTrackCount = 0;

	/** Number of notifies */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 NotifyCount = 0;

	/** Number of branching points */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 BranchingPointCount = 0;

	/** Blend in time */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float BlendInTime = 0.0f;

	/** Blend out time */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float BlendOutTime = 0.0f;

	/** Blend out trigger time (-1 = auto) */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float BlendOutTriggerTime = -1.0f;

	/** Whether root motion translation is enabled */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	bool bEnableRootMotionTranslation = true;

	/** Whether root motion rotation is enabled */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	bool bEnableRootMotionRotation = true;

	/** List of slot names used */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	TArray<FString> SlotNames;
};

/**
 * Information about a montage section
 */
USTRUCT(BlueprintType)
struct FMontageSectionInfo
{
	GENERATED_BODY()

	/** Section name */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString SectionName;

	/** Section index */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 SectionIndex = 0;

	/** Start time in seconds */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float StartTime = 0.0f;

	/** End time in seconds */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float EndTime = 0.0f;

	/** Section duration */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float Duration = 0.0f;

	/** Next section name (empty if none linked) */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString NextSectionName;

	/** Whether section loops to itself */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	bool bLoops = false;

	/** Segment count in this section */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 SegmentCount = 0;
};

/**
 * Section linking information
 */
USTRUCT(BlueprintType)
struct FSectionLink
{
	GENERATED_BODY()

	/** Source section name */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString FromSection;

	/** Target section name */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString ToSection;

	/** Whether this is a self-loop */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	bool bIsLoop = false;
};

/**
 * Information about a slot animation track
 */
USTRUCT(BlueprintType)
struct FSlotTrackInfo
{
	GENERATED_BODY()

	/** Track index */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 TrackIndex = 0;

	/** Slot name for this track */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString SlotName;

	/** Number of animation segments */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 SegmentCount = 0;

	/** Total duration of segments */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float TotalDuration = 0.0f;
};

/**
 * Information about an animation segment within a slot track
 */
USTRUCT(BlueprintType)
struct FAnimSegmentInfo
{
	GENERATED_BODY()

	/** Segment index within the track */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 SegmentIndex = 0;

	/** Path to the source animation sequence */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString AnimSequencePath;

	/** Animation name */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString AnimName;

	/** Start time in the montage timeline */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float StartTime = 0.0f;

	/** Duration in the montage */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float Duration = 0.0f;

	/** Playback rate multiplier */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float PlayRate = 1.0f;

	/** Start position within the source animation */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float AnimStartPos = 0.0f;

	/** End position within the source animation */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float AnimEndPos = 0.0f;

	/** Number of loops (0 = use full length) */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 LoopCount = 0;

	/** Whether this segment loops within its duration */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	bool bLoops = false;
};

/**
 * Information about a branching point
 */
USTRUCT(BlueprintType)
struct FBranchingPointInfo
{
	GENERATED_BODY()

	/** Index of the branching point */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	int32 Index = 0;

	/** Notify name for this branching point */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString NotifyName;

	/** Trigger time in seconds */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	float TriggerTime = 0.0f;

	/** Section this branching point is in */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	FString SectionName;
};

// ============================================================================
// SERVICE CLASS
// ============================================================================

/**
 * Animation Montage service exposed directly to Python.
 *
 * This service provides comprehensive CRUD operations for Animation Montage assets
 * including section management, slot tracks, animation segments and branching points.
 * These operations require C++ because Python's set_editor_property() returns read-only
 * copies of internal TArrays like CompositeSections and SlotAnimTracks. Plain properties
 * (Skeleton, BlendIn / BlendOut {BlendTime, BlendOption}, BlendOutTriggerTime) belong to
 * EditorToolset.ObjectToolset.GetProperties / SetProperties; duplicating a montage is
 * EditorToolset.AssetToolset.Duplicate.
 *
 * IMPORTANT: All montage_path parameters require the FULL asset path (package_name from AssetData),
 * NOT the folder path (package_path). For example:
 *   - CORRECT: "/Game/Characters/Mannequin/Montages/AM_Attack"
 *   - WRONG:   "/Game/Characters/Mannequin/Montages" (this is a folder, not an asset)
 *
 * Python Usage:
 *   import unreal
 *
 *   # List all montages for a skeleton (engine toolsets, via await call_tool in execute_python)
 *   refs = await call_tool("EditorToolset.AssetToolset.GetReferencers", assetPath="/Game/SK_Mannequin")
 *   mon = await call_tool("EditorToolset.AssetToolset.FindAssets", filter={"folderPath": "/Game", "name": "",
 *       "assetType": {"refPath": "/Script/Engine.AnimMontage"}, "bRecursive": True, "tags": [], "maxResults": 0})
 *   montages = sorted(set(refs) & set(mon))
 *
 *   # Create the montage natively (AnimMontageFactory via unreal.AssetTools.create_asset), then:
 *   path = "/Game/Montages/AM_Attack"
 *
 *   # Add sections for combo system
 *   unreal.AnimMontageService.add_section(path, "WindUp", 0.0)
 *   unreal.AnimMontageService.add_section(path, "Attack", 0.3)
 *   unreal.AnimMontageService.add_section(path, "Recovery", 0.8)
 *
 *   # Link sections for combo flow
 *   unreal.AnimMontageService.set_next_section(path, "WindUp", "Attack")
 *   unreal.AnimMontageService.set_next_section(path, "Attack", "Recovery")
 *
 *   # Add branching point for combo input window
 *   unreal.AnimMontageService.add_branching_point(path, "ComboWindow", 0.6)
 *
 * @note All methods are static and thread-safe
 * @note For notifies on montages, you can use AnimSequenceService methods (inherited)
 *
 * **C++ Source:**
 *
 * - **Plugin**: VibeUE
 * - **Module**: VibeUE
 * - **File**: UAnimMontageService.h
 */
UCLASS(BlueprintType)
class VIBEUE_API UAnimMontageService : public UToolsetDefinition
{
	GENERATED_BODY()

public:

	// ============================================================================
	// SECTION MANAGEMENT (Requires C++ - Python cannot modify TArrays)
	// ============================================================================

	/**
	 * List all sections in a montage.
	 *
	 * @param MontagePath - Path to montage
	 * @return Array of section info structs, ordered by start time
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|Sections")
	static TArray<FMontageSectionInfo> ListSections(const FString& MontagePath);

	/**
	 * Get info for a specific section by name.
	 *
	 * @param MontagePath - Path to montage
	 * @param SectionName - Name of the section
	 * @return Section info. Raises a script error (a Python exception, an MCP tool error) when it cannot be read
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|Sections")
	static FMontageSectionInfo GetSectionInfo(const FString& MontagePath, const FString& SectionName);

	/** C++ form of GetSectionInfo: false on failure, the result through the reference. Not reflected: an output reference
	 *  would become a required input of the MCP tool and its value would be dropped (#687). */
	static bool GetSectionInfo(const FString& MontagePath, const FString& SectionName, FMontageSectionInfo& OutInfo);

	/**
	 * Get section index at a specific time.
	 *
	 * @param MontagePath - Path to montage
	 * @param Time - Time in seconds
	 * @return Section index, or -1 if time is out of range
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|Sections")
	static int32 GetSectionIndexAtTime(const FString& MontagePath, float Time);

	/**
	 * Get section name at a specific time.
	 *
	 * @param MontagePath - Path to montage
	 * @param Time - Time in seconds
	 * @return Section name, or empty string if time is out of range
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|Sections")
	static FString GetSectionNameAtTime(const FString& MontagePath, float Time);

	/**
	 * Add a new section to the montage.
	 *
	 * @param MontagePath - Path to montage
	 * @param SectionName - Name for the new section (must be unique)
	 * @param StartTime - Start time in seconds (must be within montage duration)
	 * @return True if section was added successfully
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Sections")
	static bool AddSection(
		const FString& MontagePath,
		const FString& SectionName,
		float StartTime);

	/**
	 * Remove a section from the montage.
	 *
	 * @param MontagePath - Path to montage
	 * @param SectionName - Name of the section to remove
	 * @return True if successful
	 * @note Cannot remove the "Default" section if it's the only one
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Sections")
	static bool RemoveSection(const FString& MontagePath, const FString& SectionName);

	/**
	 * Rename an existing section.
	 *
	 * @param MontagePath - Path to montage
	 * @param OldName - Current section name
	 * @param NewName - New section name (must be unique)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Sections")
	static bool RenameSection(
		const FString& MontagePath,
		const FString& OldName,
		const FString& NewName);

	/**
	 * Move a section to a new start time.
	 *
	 * @param MontagePath - Path to montage
	 * @param SectionName - Name of the section
	 * @param NewStartTime - New start time in seconds
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Sections")
	static bool SetSectionStartTime(
		const FString& MontagePath,
		const FString& SectionName,
		float NewStartTime);

	/**
	 * Get the duration of a specific section.
	 *
	 * @param MontagePath - Path to montage
	 * @param SectionName - Name of the section
	 * @return Section duration, or -1 if not found
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|Sections")
	static float GetSectionLength(const FString& MontagePath, const FString& SectionName);

	// ============================================================================
	// SECTION LINKING (BRANCHING)
	// ============================================================================

	/**
	 * Get the next section that plays after the specified section.
	 *
	 * @param MontagePath - Path to montage
	 * @param SectionName - Name of the section
	 * @return Next section name, or empty string if section ends montage
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|Sections")
	static FString GetNextSection(const FString& MontagePath, const FString& SectionName);

	/**
	 * Link a section to play another section when it completes.
	 *
	 * @param MontagePath - Path to montage
	 * @param SectionName - Source section
	 * @param NextSectionName - Section to play next (empty string = end montage)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Sections")
	static bool SetNextSection(
		const FString& MontagePath,
		const FString& SectionName,
		const FString& NextSectionName);

	/**
	 * Set a section to loop to itself.
	 *
	 * @param MontagePath - Path to montage
	 * @param SectionName - Name of the section
	 * @param bLoop - True to loop, false to clear loop (section ends montage)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Sections")
	static bool SetSectionLoop(
		const FString& MontagePath,
		const FString& SectionName,
		bool bLoop);

	/**
	 * Get all section links in the montage.
	 *
	 * @param MontagePath - Path to montage
	 * @return Array of section link info structs
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|Sections")
	static TArray<FSectionLink> GetAllSectionLinks(const FString& MontagePath);

	/**
	 * Clear the link from a section (section will end the montage).
	 *
	 * @param MontagePath - Path to montage
	 * @param SectionName - Name of the section
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Sections")
	static bool ClearSectionLink(const FString& MontagePath, const FString& SectionName);

	/**
	 * Add a new slot track to the montage.
	 *
	 * @param MontagePath - Path to montage
	 * @param SlotName - Name of the animation slot (must exist in skeleton)
	 * @return Index of the new track, or -1 on failure
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Slots")
	static int32 AddSlotTrack(const FString& MontagePath, const FString& SlotName);

	/**
	 * Remove a slot track from the montage.
	 *
	 * @param MontagePath - Path to montage
	 * @param TrackIndex - Index of the track to remove
	 * @return True if successful
	 * @note Cannot remove the last slot track
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Slots")
	static bool RemoveSlotTrack(const FString& MontagePath, int32 TrackIndex);

	/**
	 * Change the slot name for a track.
	 *
	 * @param MontagePath - Path to montage
	 * @param TrackIndex - Index of the track
	 * @param NewSlotName - New slot name
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Slots")
	static bool SetSlotName(
		const FString& MontagePath,
		int32 TrackIndex,
		const FString& NewSlotName);

	/**
	 * Add an animation segment to a slot track.
	 *
	 * @param MontagePath - Path to montage
	 * @param TrackIndex - Slot track to add to
	 * @param AnimSequencePath - Path to the animation sequence
	 * @param StartTime - Start position in montage timeline
	 * @param PlayRate - Playback rate multiplier (1.0 = normal)
	 * @return Index of the new segment, or -1 on failure
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Segments")
	static int32 AddAnimSegment(
		const FString& MontagePath,
		int32 TrackIndex,
		const FString& AnimSequencePath,
		float StartTime,
		float PlayRate = 1.0f);

	/**
	 * Remove an animation segment from a slot track.
	 *
	 * @param MontagePath - Path to montage
	 * @param TrackIndex - Index of the slot track
	 * @param SegmentIndex - Index of the segment to remove
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Segments")
	static bool RemoveAnimSegment(
		const FString& MontagePath,
		int32 TrackIndex,
		int32 SegmentIndex);

	/**
	 * Set the start time of a segment in the montage timeline.
	 *
	 * @param MontagePath - Path to montage
	 * @param TrackIndex - Index of the slot track
	 * @param SegmentIndex - Index of the segment
	 * @param NewStartTime - New start time in seconds
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Segments")
	static bool SetSegmentStartTime(
		const FString& MontagePath,
		int32 TrackIndex,
		int32 SegmentIndex,
		float NewStartTime);

	/**
	 * Set the playback rate of a segment.
	 *
	 * @param MontagePath - Path to montage
	 * @param TrackIndex - Index of the slot track
	 * @param SegmentIndex - Index of the segment
	 * @param PlayRate - New playback rate
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Segments")
	static bool SetSegmentPlayRate(
		const FString& MontagePath,
		int32 TrackIndex,
		int32 SegmentIndex,
		float PlayRate);

	/**
	 * Set the start position within the source animation.
	 *
	 * @param MontagePath - Path to montage
	 * @param TrackIndex - Index of the slot track
	 * @param SegmentIndex - Index of the segment
	 * @param AnimStartPos - Time in source animation to start playing from
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Segments")
	static bool SetSegmentStartPosition(
		const FString& MontagePath,
		int32 TrackIndex,
		int32 SegmentIndex,
		float AnimStartPos);

	/**
	 * Set the end position within the source animation.
	 *
	 * @param MontagePath - Path to montage
	 * @param TrackIndex - Index of the slot track
	 * @param SegmentIndex - Index of the segment
	 * @param AnimEndPos - Time in source animation to stop playing
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Segments")
	static bool SetSegmentEndPosition(
		const FString& MontagePath,
		int32 TrackIndex,
		int32 SegmentIndex,
		float AnimEndPos);

	/**
	 * Set how many times a segment loops.
	 *
	 * @param MontagePath - Path to montage
	 * @param TrackIndex - Index of the slot track
	 * @param SegmentIndex - Index of the segment
	 * @param LoopCount - Number of loops (0 = play once, no loop)
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Segments")
	static bool SetSegmentLoopCount(
		const FString& MontagePath,
		int32 TrackIndex,
		int32 SegmentIndex,
		int32 LoopCount);

	/**
	 * Remove a notify from the montage.
	 *
	 * @param MontagePath - Path to montage
	 * @param NotifyIndex - Index of the notify to remove
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Notifies")
	static bool RemoveNotify(const FString& MontagePath, int32 NotifyIndex);

	/**
	 * Set the trigger time for a notify.
	 *
	 * @param MontagePath - Path to montage
	 * @param NotifyIndex - Index of the notify
	 * @param NewTime - New trigger time in seconds
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Notifies")
	static bool SetNotifyTriggerTime(
		const FString& MontagePath,
		int32 NotifyIndex,
		float NewTime);

	/**
	 * Move a notify to a section's start time.
	 * Montages link notifies to slot segments, not sections: moving the section later
	 * does not move the notify.
	 *
	 * @param MontagePath - Path to montage
	 * @param NotifyIndex - Index of the notify
	 * @param SectionName - Name of the section whose start time to use
	 * @return True if the notify was moved
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|Notifies")
	static bool SetNotifyLinkToSection(
		const FString& MontagePath,
		int32 NotifyIndex,
		const FString& SectionName);

	/**
	 * Add a branching point to the montage.
	 * Branching points are frame-accurate notifies used for gameplay decisions.
	 *
	 * @param MontagePath - Path to montage
	 * @param NotifyName - Name for the branching point event
	 * @param TriggerTime - Time in seconds when the event fires
	 * @return Index of the new branching point, or -1 on failure
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|BranchingPoints")
	static int32 AddBranchingPoint(
		const FString& MontagePath,
		const FString& NotifyName,
		float TriggerTime);

	// ============================================================================
	// ROOT MOTION
	// ============================================================================

	/**
	 * Get whether root motion translation is enabled.
	 *
	 * @param MontagePath - Path to montage
	 * @return True if root motion translation is enabled
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|RootMotion")
	static bool GetEnableRootMotionTranslation(const FString& MontagePath);

	/**
	 * Enable or disable root motion translation.
	 *
	 * @param MontagePath - Path to montage
	 * @param bEnable - Whether to enable root motion translation
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|RootMotion")
	static bool SetEnableRootMotionTranslation(const FString& MontagePath, bool bEnable);

	/**
	 * Get whether root motion rotation is enabled.
	 *
	 * @param MontagePath - Path to montage
	 * @return True if root motion rotation is enabled
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|RootMotion")
	static bool GetEnableRootMotionRotation(const FString& MontagePath);

	/**
	 * Enable or disable root motion rotation.
	 *
	 * @param MontagePath - Path to montage
	 * @param bEnable - Whether to enable root motion rotation
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Animation|Montage|RootMotion")
	static bool SetEnableRootMotionRotation(const FString& MontagePath, bool bEnable);

	/**
	 * Get the root motion accumulated from the montage start to Time (all segments of the root-motion track).
	 *
	 * @param MontagePath - Path to montage
	 * @param Time - Time in seconds
	 * @return Root motion transform. Raises a script error (a Python exception, an MCP tool error) when it cannot be read
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Animation|Montage|RootMotion")
	static FTransform GetRootMotionAtTime(const FString& MontagePath, float Time);

	/** C++ form of GetRootMotionAtTime: false on failure, the result through the reference. Not reflected: an output reference
	 *  would become a required input of the MCP tool and its value would be dropped (#687). */
	static bool GetRootMotionAtTime(const FString& MontagePath, float Time, FTransform& OutTransform);

private:
	// ============================================================================
	// PRIVATE HELPERS
	// ============================================================================

	/** Load an animation montage from path */
	static class UAnimMontage* LoadMontage(const FString& MontagePath);

	/** Load an animation montage to read it: LoadMontage, but during PIE (where editor scripting refuses every load) directly */
	static class UAnimMontage* LoadMontageToRead(const FString& MontagePath);

	/** Mark montage as modified for undo/redo and saving */
	static void MarkMontageModified(class UAnimMontage* Montage);

	/** Validate section name exists in montage */
	static bool ValidateSection(class UAnimMontage* Montage, const FString& SectionName);

	/** Validate track index is in range */
	static bool ValidateTrackIndex(class UAnimMontage* Montage, int32 TrackIndex);

	/** Validate segment index within track */
	static bool ValidateSegmentIndex(class UAnimMontage* Montage, int32 TrackIndex, int32 SegmentIndex);

	/** Fill montage info struct from montage */
	static void FillMontageInfo(class UAnimMontage* Montage, FMontageInfo& OutInfo);
};
