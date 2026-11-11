// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UInputService.generated.h"

/**
 * Information about discovered input types.
 * Python access: types = unreal.InputService.discover_types()
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
 * Input Service - Enhanced Input ASSET authoring, plus action injection into PIE.
 *
 * Kept: adding modifiers and triggers to a mapping by INDEX (add_modifier / add_trigger /
 * add_action_trigger) and the type/key discovery calls (discover_types, get_available_keys) that
 * return the strings they accept. Native Python covers the rest: unreal.AssetTools.create_asset
 * for actions and contexts, InputMappingContext.map_key / unmap_key for key mappings, and
 * EditorToolset.ObjectToolset Get/SetProperties to read or edit mappings. map_key returns a COPY:
 * changes to it do not persist until the mappings array is written back.
 *
 * Use the engine for everything else:
 *  - pressing an action OR a raw key in a running session, for any duration: `InputToolset`
 *    (ApplyInput takes Entries -- each entry one Action or one Key, with its value -- and
 *    DurationSeconds; IsInputApplied reads back whether they are applied). This is what replaced
 *    VibeUE's held-action and raw-key injection.
 *  - any property on an input asset these calls do not reach: `EditorToolset.ObjectToolset`
 *    (GetProperties / SetProperties / ListProperties)
 *  - finding, saving and validating the assets: `EditorToolset.AssetToolset`
 *    (FindAssets / SaveAsset / ValidateAssets)
 *
 * Python Usage:
 *   import unreal
 *
 *   types  = unreal.InputService.discover_types()
 *   # Create assets with unreal.AssetTools.create_asset, map keys with
 *   # unreal.InputMappingContext.map_key, and inject with InputToolset.ApplyInput.
 */
UCLASS(BlueprintType)
class VIBEUE_API UInputService : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	// =================================================================
	// Reflection (reflection_discover_types)
	// =================================================================

	/**
	 * Discover available Enhanced Input types.
	 * Maps to action="reflection_discover_types"
	 *
	 * @return Discovery result with action value types, modifiers, and triggers
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category ="VibeUE|Input")
	static FInputTypeDiscoveryResult DiscoverTypes();

	/**
	 * Get list of available input keys.
	 * Maps to action="mapping_get_available_keys"
	 *
	 * @param Filter - Optional filter string to narrow results
	 * @return Array of available key names
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category ="VibeUE|Input")
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
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category ="VibeUE|Input")
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
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category ="VibeUE|Input")
	static bool AddTrigger(
		const FString& ContextPath,
		int32 MappingIndex,
		const FString& TriggerType,
		const FString& PropertiesJson = TEXT(""));

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
	UFUNCTION(BlueprintCallable, meta = (AICallable), Category ="VibeUE|Input")
	static FString AddActionTrigger(
		const FString& ActionPath,
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
};
