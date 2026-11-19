// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UPerformanceService.generated.h"

/**
 * Performance service — what UE 5.9's native PerfToolset lacks: trace+log analysis, forced hitches to
 * validate a verdict, trace-attached standalone play, and editor background-throttling control.
 *
 * Recommended flow: PerfToolset.StatsToolset.GetFrameTimings FIRST (judge CPU vs GPU bound from its
 * numbers) → ConsoleToolset.RunConsoleCommand "Trace.Start cpu,gpu,frame" → reproduce the workload →
 * "Trace.Stop" → Analyse. For a representative reading, profile under PIE or a standalone session,
 * not the bare editor viewport. Methods return a JSON string.
 */
UCLASS(BlueprintType)
class VIBEUE_API UPerformanceService : public UToolsetDefinition
{
	GENERATED_BODY()

public:








	/**
	 * Read back a trace and/or the log and return a perf summary (frame stats, worst frames, notable
	 * log lines, hitches). "both" succeeds only when both sources succeed; a usable single source is
	 * returned as status="partial", success=false, partial=true with per-source results.
	 * @param Source "trace", "logs", or "both" (default).
	 * @param File Optional override path; empty uses the last trace started/stopped.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Performance")
	static FString Analyse(const FString& Source = TEXT("both"), const FString& File = TEXT(""));

	/**
	 * Launch the game as a separate standalone process with a direct-to-file trace attached. The
	 * initial response is pending (not successful) until GetStandaloneStatus verifies the child PID
	 * and exact non-empty trace destination.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Execute"), Category = "VibeUE|Performance")
	static FString StartStandalone(const FString& Name = TEXT("standalone_capture"), const FString& Channels = TEXT(""));

	/** Stop the tracked standalone process and verify graceful exit plus exact trace finalization. */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Execute"), Category = "VibeUE|Performance")
	static FString StopStandalone();

	/** Report tracked session/PID/map/path provenance and capture/finalization verification state. */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Performance")
	static FString GetStandaloneStatus();



	/**
	 * Enable/disable editor background CPU throttling for this session (issue #549).
	 *
	 * An unfocused editor throttles to a few FPS, which makes unattended PIE verification useless —
	 * and neither `t.IdleWhenNotInForeground` nor `Slate.bAllowThrottling` controls this knob (it is
	 * UEditorPerformanceSettings.bThrottleCPUWhenNotForeground, not exposed to Python). Pass
	 * bEnabled=false before automated PIE runs; pass true to restore the default. Also defeats the
	 * minimized-window render disable. Session-only — nothing is saved to EditorSettings.ini.
	 */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Write"), Category = "VibeUE|Performance")
	static FString SetBackgroundThrottling(bool bEnabled);

	/** Report the current background-throttling state (see SetBackgroundThrottling). */
	UFUNCTION(BlueprintCallable, meta = (AICallable, AIAccessMode = "Read"), Category = "VibeUE|Performance")
	static FString GetBackgroundThrottling();

	/**
	 * Remove the ShouldDisableCPUThrottlingDelegates entry SetBackgroundThrottling(false) registered and, if
	 * that override is still on, put bThrottleCPUWhenNotForeground back to its value from before it. Called
	 * from the module's OnPreExit (while GEditor still exists) and ShutdownModule. The entry is a lambda INTO
	 * THIS DLL, so it must not outlive an unload (plugin reload / Live Coding). Safe to call twice and with
	 * GEditor null (then only the handle is reset). Not a UFUNCTION. GAME-THREAD ONLY.
	 */
	static void ShutdownThrottlingHook();
};
