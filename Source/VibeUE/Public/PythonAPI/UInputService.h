// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UInputService.generated.h"

/**
 * Information about discovered input types.
 * Types come from EditorToolset.ObjectToolset.SearchSubclasses (InputModifier / InputTrigger).
 * 
 * Properties:
 * - action_value_types (Array[str]): Available value types: "Boolean", "Axis1D", "Axis2D", "Axis3D"
 * - modifier_types (Array[str]): Available modifiers: "Negate", "DeadZone", "Scalar", etc.
 * - trigger_types (Array[str]): Available triggers: "Pressed", "Released", "Hold", "Tap", etc.
 * 
 * NOTE: Use action_value_types, NOT value_types!
 */
USTRUCT(BlueprintType)
struct FInputTypeDiscoveryResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Input")
	TArray<FString> ActionValueTypes;

	UPROPERTY(BlueprintReadWrite, Category = "Input")
	TArray<FString> ModifierTypes;

	UPROPERTY(BlueprintReadWrite, Category = "Input")
	TArray<FString> TriggerTypes;
};

/**
 * Result of creating an input action or mapping context.
 * Python access: result = unreal.InputService.create_action(...)
 * 
 * Properties:
 * - success (bool): True if asset was created successfully
 * - asset_path (str): Full path to created asset (e.g., "/Game/Input/IA_Jump")
 * - error_message (str): Error message if failed (empty if success)
 * 
 * IMPORTANT: This is a struct, not a string. Use result.asset_path to get the path.
 */
USTRUCT(BlueprintType)
struct FInputCreateResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Input")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "Input")
	FString AssetPath;

	UPROPERTY(BlueprintReadWrite, Category = "Input")
	FString ErrorMessage;
};

/**
 * Input Service - Enhanced Input ASSET authoring, plus action injection into PIE.
 *
 * Kept: adding modifiers and triggers to a mapping by INDEX (add_modifier / add_trigger) and the
 * key discovery call (get_available_keys) that returns the strings it accepts. Modifier / trigger
 * types: ObjectToolset.SearchSubclasses; an action-level trigger is a native trigger object appended
 * to the Input Action's `triggers`. Native Python covers the rest: unreal.AssetTools.create_asset
 * for actions and contexts, InputMappingContext.map_key / unmap_key for key mappings, and
 * EditorToolset.ObjectToolset Get/SetProperties to read or edit mappings. map_key returns a COPY:
 * changes to it do not persist until the mappings array is written back.
 *
 * Use the engine for everything else:
 *  - pressing an action OR a raw key in a running session, for any duration: `InputToolset`
 *    (ApplyInput takes Entries -- each entry one Action or one Key, with its value -- and
 *    DurationSeconds; IsInputApplied reads back whether they are applied). This is what replaced
 *    VibeUE's held-action and raw-key injection.
 *  - any property on an input asset: `EditorToolset.ObjectToolset` (GetProperties / SetProperties /
 *    ListProperties). An Input Action's info is GetProperties(instance=<action path>, properties=
 *    ["ValueType","bConsumeInput","bTriggerWhenPaused","ActionDescription"]) and configuring it is
 *    SetProperties(instance=<action path>, values=<JSON of the same names>)
 *  - finding, existence, saving and validating the assets: `EditorToolset.AssetToolset`
 *    (FindAssets with filter={"assetType": "/Script/EnhancedInput.InputAction", "maxResults": 0} or
 *    {"assetType": "/Script/EnhancedInput.InputMappingContext", "maxResults": 0} for all of them
 *    -- maxResults is a field of the filter object, 0 = no limit / Exists / SaveAsset / ValidateAssets)
 *
 * Python Usage:
 *   import unreal
 *
 *   keys   = unreal.InputService.get_available_keys()
 *   # Create assets with unreal.AssetTools.create_asset, map keys with
 *   # unreal.InputMappingContext.map_key, and inject with InputToolset.ApplyInput.
 */
UCLASS(BlueprintType)
class VIBEUE_API UInputService : public UToolsetDefinition
{
	GENERATED_BODY()

public:

	/**
	 * Get list of available input keys.
	 * Maps to action="mapping_get_available_keys"
	 *
	 * @param Filter - Optional filter string to narrow results
	 * @return Array of available key names
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category ="VibeUE|Input")
	static TArray<FString> GetAvailableKeys(const FString& Filter = TEXT(""));

	// =================================================================
	// Modifier Management
	// =================================================================

	/**
	 * Add a modifier to a key mapping.
	 * Maps to action="mapping_add_modifier"
	 *
	 * @param ContextPath - Full path to the Mapping Context
	 * @param MappingIndex - Index of the mapping
	 * @param ModifierType - Type of modifier (e.g., "Negate", "Scalar", "DeadZone")
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|Input")
	static bool AddModifier(
		const FString& ContextPath,
		int32 MappingIndex,
		const FString& ModifierType);

	// =================================================================
	// Trigger Management
	// =================================================================

	/**
	 * Add a trigger to a key mapping.
	 * Maps to action="mapping_add_trigger"
	 *
	 * @param ContextPath - Full path to the Mapping Context
	 * @param MappingIndex - Index of the mapping
	 * @param TriggerType - Type of trigger (e.g., "Pressed", "Released", "Down", "Hold")
	 * @param PropertiesJson - Optional trigger settings as a JSON object, e.g.
	 *        {"HoldTimeThreshold": 0.5, "bIsOneShot": true}. Names match the C++ property or its
	 *        snake_case form (hold_time_threshold, is_one_shot). Only the settings the trigger's Details
	 *        panel shows can be set: runtime state (HeldDuration, LastValue, bShouldAlwaysTick, ...) is
	 *        refused, and a number outside the property's ClampMin/ClampMax (HoldTimeThreshold >= 0) is
	 *        refused, naming the range. An unknown name, a refused setting or a bad value fails the call
	 *        and adds nothing. A running PIE session keeps its copy of the triggers: restart PIE to see it.
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|Input")
	static bool AddTrigger(
		const FString& ContextPath,
		int32 MappingIndex,
		const FString& TriggerType,
		const FString& PropertiesJson = TEXT(""));


	/**
	 * End every action and key hold this service still has filed (injections stopped, pending releases and
	 * the release grace cancelled; no key-up is sent) and unhook the FEditorDelegates::EndPIE handler and the
	 * GEditor->ShouldDisableCPUThrottlingDelegates entry the holds registered. Called from the module's
	 * OnPreExit (while GEditor still exists) and ShutdownModule. Both callbacks, and the hold tickers, are
	 * code INTO THIS DLL, so they must not outlive an unload (plugin reload / Live Coding) — the same
	 * teardown as UPIEActorService::ShutdownEndPIEHook. Safe to call twice and with GEditor null (then the
	 * throttling handle is only reset: the array went with the editor engine). Not a UFUNCTION.
	 * GAME-THREAD ONLY.
	 */
	static void ShutdownInjectionHooks();

private:
	/** Helper to load an Input Action */
	static class UInputAction* LoadInputAction(const FString& ActionPath);
	
	/** Helper to load a Mapping Context */
	static class UInputMappingContext* LoadMappingContext(const FString& ContextPath);
	
	/** Helper to find a key by name */
	static FKey FindKeyByName(const FString& KeyName);
public:
	// Restored from upstream: the native replacement does not do all of this (cut audit).
	/**
	 * Add a trigger to an Input Action's own trigger list (applies to every mapping of the action).
	 *
	 * @param ActionPath - Full path to the Input Action
	 * @param TriggerType - Type of trigger (e.g., "Pressed", "Hold", "Tap")
	 * @param PropertiesJson - Optional trigger settings as a JSON object, as for add_trigger
	 * @return JSON: {success, action, trigger, trigger_index, note} or {success:false, error_code, error_message}
	 *         error_code: PIE_ACTIVE (stop PIE first: assets cannot be loaded for editing during a play session),
	 *         ACTION_NOT_FOUND, TRIGGER_TYPE_NOT_FOUND, BAD_PROPERTIES
	 *
	 * Example:
	 *   unreal.InputService.add_action_trigger("/Game/Input/IA_Block", "Hold", '{"hold_time_threshold": 0.4}')
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|Input")
	static FString AddActionTrigger(
		const FString& ActionPath,
		const FString& TriggerType,
		const FString& PropertiesJson = TEXT(""));

	/**
	 * Add a key mapping to a context.
	 * Maps to action="mapping_add_key_mapping"
	 *
	 * @param ContextPath - Full path to the Mapping Context
	 * @param ActionPath - Full path to the Input Action
	 * @param KeyName - Name of the key (e.g., "SpaceBar", "W", "Gamepad_LeftTrigger")
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|Input")
	static bool AddKeyMapping(
		const FString& ContextPath,
		const FString& ActionPath,
		const FString& KeyName);

	/**
	 * Create a new Input Action asset.
	 * Maps to action="action_create"
	 *
	 * @param ActionName - Name for the new action. An invalid asset name (empty, or with a space, '.', '/' or another
	 *                     character asset names cannot hold) is refused with the reason
	 * @param AssetPath - Folder to create the asset in (e.g., "/Game/Input"). A path that starts with its mount point
	 *                    is kept; a bare folder ("Input") goes under /Game; a folder under no mounted content root
	 *                    (/Temp/X) is refused with the reason, and so are /Engine (the installed engine's content) and
	 *                    /Script. Plugin content roots are allowed
	 * @param ValueType - Value type: "Boolean" (alias "Digital"), "Axis1D", "Axis2D", "Axis3D" — same names discover_types returns
	 * @return Create result with asset path
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|Input")
	static FInputCreateResult CreateAction(
		const FString& ActionName,
		const FString& AssetPath,
		const FString& ValueType = TEXT("Axis1D"));

	/**
	 * Create a new Input Mapping Context.
	 * Maps to action="mapping_create_context"
	 *
	 * @param ContextName - Name for the new context, checked as create_action checks its name
	 * @param AssetPath - Folder to create the asset in, taken as create_action takes it
	 * @param Priority - Context priority (higher = processed first)
	 * @return Create result with asset path
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|Input")
	static FInputCreateResult CreateMappingContext(
		const FString& ContextName,
		const FString& AssetPath,
		int32 Priority = 0);

	/**
	 * Remove a key mapping from a context.
	 * Maps to action="mapping_remove_mapping"
	 *
	 * @param ContextPath - Full path to the Mapping Context
	 * @param MappingIndex - Index of the mapping to remove
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|Input")
	static bool RemoveMapping(
		const FString& ContextPath,
		int32 MappingIndex);

	/**
	 * Remove a modifier from a key mapping.
	 * Maps to action="mapping_remove_modifier"
	 *
	 * @param ContextPath - Full path to the Mapping Context
	 * @param MappingIndex - Index of the mapping
	 * @param ModifierIndex - Index of the modifier to remove
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|Input")
	static bool RemoveModifier(
		const FString& ContextPath,
		int32 MappingIndex,
		int32 ModifierIndex);

	/**
	 * Remove a trigger from a key mapping.
	 * Maps to action="mapping_remove_trigger"
	 *
	 * @param ContextPath - Full path to the Mapping Context
	 * @param MappingIndex - Index of the mapping
	 * @param TriggerIndex - Index of the trigger to remove
	 * @return True if successful
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category ="VibeUE|Input")
	static bool RemoveTrigger(
		const FString& ContextPath,
		int32 MappingIndex,
		int32 TriggerIndex);

	/**
	 * Start holding an action INDEFINITELY, until stop_injection releases it or PIE ends. No duration.
	 *
	 * This is the one input hold the engine cannot express. `InputToolset.ApplyInput` takes a
	 * DurationSeconds and returns an async result that completes only once the hold has ended, so the
	 * hold and the agent's turn are the same span: you cannot hold an input and then do anything else
	 * while it is held. Use ApplyInput for any hold of known length (it also presses several
	 * actions/keys together, which this does not) and use this when the release depends on something
	 * you have to observe first -- hold forward, read where the character got to, then release.
	 *
	 * Calling it twice for the same action and PIE world restarts the hold rather than stacking.
	 * The hold registers in this service's table, so shutdown_injection_hooks and the EndPIE hook
	 * both drop it; a hold can never outlive its PIE session.
	 *
	 * @param ActionPath - Input Action asset path (/Game/Input/IA_MoveForward or full object path)
	 * @param X,Y,Z - The held value; maps onto the action's value type (Boolean uses X != 0)
	 * @param PieInstance - Which PIE world, as for inject_action; -1 = the first PIE world with a local player
	 * @return JSON: {success, action, pie_instance (the instance used)} or
	 *         {success:false, error_code, error_message}; same error codes as inject_action.
	 *
	 * Example:
	 *   unreal.InputService.start_injection("/Game/Input/IA_MoveForward")   # walk
	 *   # ... other tool calls; read the pawn's location ...
	 *   unreal.InputService.stop_injection("/Game/Input/IA_MoveForward")    # stop walking
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Input|PIE")
	static FString StartInjection(const FString& ActionPath, float X = 1.0f, float Y = 0.0f, float Z = 0.0f, int32 PieInstance = -1);

	/**
	 * Release an action hold filed by start_injection. The hold is found by the action it names and the
	 * PIE world, so /Game/Input/IA_X and /Game/Input/IA_X.IA_X are the same hold.
	 *
	 * A hold started with the engine's `InputToolset` ApplyInput is Epic's own, is not visible here,
	 * and ends on its own DurationSeconds.
	 *
	 * @param ActionPath - Input Action asset path
	 * @param PieInstance - Which PIE world, as for inject_action; -1 = the first PIE world with a local player
	 * @return JSON: {success, action, was_active, pie_instance (the instance used)} or
	 *         {success:false, error_code, error_message}. was_active is false if nothing was held
	 *         under that action, and once PIE has ended.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Input|PIE")
	static FString StopInjection(const FString& ActionPath, int32 PieInstance = -1);

};
