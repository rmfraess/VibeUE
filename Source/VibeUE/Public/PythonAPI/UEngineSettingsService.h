// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UEngineSettingsService.generated.h"



/**
 * Play-In-Editor multiplayer settings snapshot.
 * Read the live values from Python with unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).get_play_settings().
 *
 * Properties:
 * - success (bool): Whether the settings could be read
 * - error_message (str): Error message if failed (empty if success)
 * - net_mode (str): PIE net mode: "Standalone" | "ListenServer" | "Client" (maps EPlayNetMode).
 *                   "Client" is dedicated-server PIE — the editor's "Play As Client", where a
 *                   windowless dedicated server is spawned behind the scenes.
 * - num_clients (int): Number of client windows to open (PlayNumberOfClients)
 * - run_under_one_process (bool): Spawn all player windows in a single UE process
 * - launch_separate_server (bool): The "Launch Separate Server" toggle. UE 5.8's
 *                   ULevelEditorPlaySettings has no bDedicatedServer field; dedicated-server PIE is
 *                   NetMode="Client". This is the closest standing flag and is reported for context.
 * - last_play_mode (str): Last executed play-mode type (e.g. "PlayMode_InViewPort"), or "" if unknown
 */
USTRUCT(BlueprintType)
struct FPIESettingsInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "EngineSettings")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadWrite, Category = "EngineSettings")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadWrite, Category = "EngineSettings")
	FString NetMode;

	UPROPERTY(BlueprintReadWrite, Category = "EngineSettings")
	int32 NumClients = 0;

	UPROPERTY(BlueprintReadWrite, Category = "EngineSettings")
	bool bRunUnderOneProcess = false;

	UPROPERTY(BlueprintReadWrite, Category = "EngineSettings")
	bool bLaunchSeparateServer = false;

	UPROPERTY(BlueprintReadWrite, Category = "EngineSettings")
	FString LastPlayMode;
};

/**
 * Engine Settings Service - no Python methods. This service has NO AI tools.
 *
 * Engine settings are engine-native on the 5-9 branch, so nothing here is a tool any more. Cvars,
 * console commands and scalability are `ConsoleToolset`; everything in Project Settings or Editor
 * Preferences -- ULevelEditorPlaySettings included, at Editor / LevelEditor / PlayIn -- is
 * `ConfigSettingsToolset`; a raw ini key with no registered section is `FileSystemToolset`. Not
 * ObjectToolset: a UDeveloperSettings object is a native CDO, which SetProperties refuses. See the
 * `engine-settings` skill.
 *
 * Nothing here is exposed to Python or MCP. Read the PIE settings with
 * `unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).get_play_settings()` and write them with
 * ConfigSettingsToolset.SetSectionProperties (Editor / LevelEditor / PlayIn); Content/Python/vibeue.py's
 * GateSession does exactly that to apply and restore the PIE net mode from inside execute_python.
 */
UCLASS(BlueprintType)
class VIBEUE_API UEngineSettingsService : public UToolsetDefinition
{
	GENERATED_BODY()

public:

	// =================================================================
	// Console Variables (CVars)
	// =================================================================






	// =================================================================
	// Batch Operations
	// =================================================================


	// =================================================================
	// Direct Engine INI Access
	// =================================================================






	// =================================================================
	// Scalability Settings
	// =================================================================




	// =================================================================
	// Persistence
	// =================================================================



private:

	/** Get console variable flags as string */
	static FString GetCVarFlagsString(IConsoleVariable* CVar);

	/** Get console variable type as string */
	static FString GetCVarTypeString(IConsoleVariable* CVar);
};
