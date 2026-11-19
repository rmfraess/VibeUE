// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UEngineSettingsService.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UObject/UObjectIterator.h"
#include "Engine/RendererSettings.h"
#include "Engine/Engine.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "AudioDeviceManager.h"
#include "Sound/AudioSettings.h"
#include "GameFramework/GameUserSettings.h"
#include "Scalability.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogEngineSettingsService, Log, All);

// =================================================================
// File-local helpers
// =================================================================

namespace
{
	FString GetEngineConfigFilePath(const FString& ConfigFile)
	{
		if (ConfigFile.IsEmpty())
		{
			return FString();
		}

		// Check if already an absolute path
		if (!FPaths::IsRelative(ConfigFile))
		{
			return ConfigFile;
		}

		// Handle "DefaultXXX.ini" format - project config
		if (ConfigFile.StartsWith(TEXT("Default")))
		{
			FString ProjectConfigDir = FPaths::ProjectConfigDir();
			return ProjectConfigDir / ConfigFile;
		}

		// Handle base engine config names
		FString ProjectConfigDir = FPaths::ProjectConfigDir();
		FString FullPath = ProjectConfigDir / TEXT("Default") + ConfigFile;

		if (FPaths::FileExists(FullPath))
		{
			return FullPath;
		}

		// Try as-is
		return ProjectConfigDir / ConfigFile;
	}

}


FString UEngineSettingsService::GetCVarFlagsString(IConsoleVariable* CVar)
{
	if (!CVar)
	{
		return TEXT("");
	}

	TArray<FString> Flags;

	EConsoleVariableFlags CVarFlags = CVar->GetFlags();

	if (EnumHasAnyFlags(CVarFlags, ECVF_RenderThreadSafe))
	{
		Flags.Add(TEXT("RenderThreadSafe"));
	}
	if (EnumHasAnyFlags(CVarFlags, ECVF_Scalability))
	{
		Flags.Add(TEXT("Scalability"));
	}
	if (EnumHasAnyFlags(CVarFlags, ECVF_ReadOnly))
	{
		Flags.Add(TEXT("ReadOnly"));
	}
	if (EnumHasAnyFlags(CVarFlags, ECVF_Cheat))
	{
		Flags.Add(TEXT("Cheat"));
	}

	return FString::Join(Flags, TEXT(", "));
}

FString UEngineSettingsService::GetCVarTypeString(IConsoleVariable* CVar)
{
	if (!CVar)
	{
		return TEXT("unknown");
	}

	// Try to determine type from the cvar
	if (CVar->IsVariableInt())
	{
		return TEXT("int");
	}
	if (CVar->IsVariableFloat())
	{
		return TEXT("float");
	}
	if (CVar->IsVariableString())
	{
		return TEXT("string");
	}
	if (CVar->IsVariableBool())
	{
		return TEXT("bool");
	}

	return TEXT("unknown");
}

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


