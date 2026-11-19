// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UPerformanceTriageSkill.h"

UPerformanceTriageSkill::UPerformanceTriageSkill()
{
	// Shown in ListSkills — keep it one line so the assistant can decide whether to open the skill.
	Description = TEXT(
		"Frame-rate triage & validation: determine whether the game is CPU (game/render) or GPU bound "
		"BEFORE optimising, gate against an FPS budget, capture Unreal Insights traces, and validate the "
		"verdict with deliberately forced hitches.");

	// Read via GetSkills -> GetDetails(). This is the strategy the per-tool descriptions can't carry.
	Instructions = FString(
		TEXT("Frame-rate triage. Tool names are UE 5.9 full names: call them with call_tool / await call_tool(...).\n")
		TEXT("\n")
		TEXT("# Golden rule\n")
		TEXT("ALWAYS read PerfToolset.StatsToolset.GetFrameTimings FIRST. Optimising the GPU is wasted effort if the frame is game- or render-thread bound, and vice-versa. Never start a trace or suggest a fix before you know the bound thread.\n")
		TEXT("\n")
		TEXT("# 1. Triage: GetFrameTimings\n")
		TEXT("It returns frameMs, gameThreadMs, renderThreadMs, rhiThreadMs and gpuMs for ONE frame, unsmoothed. Take several readings and judge the bound yourself: the largest of the four thread/GPU times is the bottleneck.\n")
		TEXT("- Two within ~10% of each other: treat BOTH as bottlenecks; take more readings or trace both.\n")
		TEXT("- gpuMs == 0: GPU timing unavailable, so a CPU verdict is UNCONFIRMED. Resolution test: 'r.ScreenPercentage 50' should noticeably raise FPS if truly GPU-bound.\n")
		TEXT("- Budget: 1000/target_fps ms (16.7 at 60). Compare frameMs and each thread against it.\n")
		TEXT("- Without PIE you are reading the EDITOR viewport, not the game. Start PIE (EditorToolset.EditorAppToolset.StartPIE) or VibeUE.PerformanceService.StartStandalone and park in a representative/worst spot first.\n")
		TEXT("- Per-scope detail: PerfToolset.StatsToolset.GetFrameTree; counters such as draw calls: GetStatCounters.\n")
		TEXT("\n")
		TEXT("# 2. Act on the verdict\n")
		TEXT("- GPU: profile the GPU (PerfToolset.GpuProfilerToolset, or a trace with channels frame,gpu,cpu). Levers: shadows (Virtual Shadow Maps), Lumen, translucency, post-process, ScreenPercentage.\n")
		TEXT("- RenderThread: too many draw calls / primitives or dynamic shadow-casting lights. Check 'stat scenerendering'. Levers: merge/instance meshes, Nanite, cut dynamic lights. Dropping r.ScreenPercentage will NOT help.\n")
		TEXT("- RHIThread: GPU command submission. Check 'stat rhi'. Levers: cut draw calls via instancing / HISM / Nanite, reduce material and state permutations. Dropping r.ScreenPercentage will NOT help.\n")
		TEXT("- GameThread: Tick / Blueprint / AI / animation cost. GetFrameTree shows the expensive scopes. Levers: throttle Tick, fewer ticking actors/AI, cut expensive BP tick. Dropping r.ScreenPercentage will NOT help.\n")
		TEXT("\n")
		TEXT("# 3. Capture a trace for detail\n")
		TEXT("ConsoleToolset.ConsoleToolset.RunConsoleCommand('Trace.Start frame,cpu,gpu') -> reproduce the workload -> RunConsoleCommand('Trace.Stop') -> VibeUE.PerformanceService.Analyse('both'). For a representative reading prefer StartStandalone (a separate game process) over the bare editor.\n")
		TEXT("\n")
		TEXT("# 4. Validate the verdict (ForceHitch)\n")
		TEXT("VibeUE.PerformanceService.ForceHitch deliberately stalls a KNOWN thread. The CPU paths (game/render/both) self-measure and return observed_peak_*_ms + verdict_matched_expect in the SAME response: validate from that, do NOT follow up with GetFrameTimings (that read races the stall and misses it). The gpu path is async: read GetFrameTimings on a following frame and compare against `expect`:\n")
		TEXT("- ForceHitch('game', 250)   -> expect GameThread\n")
		TEXT("- ForceHitch('render', 250) -> expect RenderThread\n")
		TEXT("- ForceHitch('both', 250)   -> expect game and render contested\n")
		TEXT("- ForceHitch('gpu')         -> expect GPU (scene-dependent; supersamples via r.ScreenPercentage, auto-restored)\n")
		TEXT("Use frames>1 to sustain the hitch and reproduce jitter rather than a single spike.\n"));
}
