---
name: sound-cues
display_name: Sound Cue Editor
description: Create and modify SoundCue assets — add/connect nodes (mixer, random, delay, attenuation, modulator, etc.) and set audio properties. Use when the user asks to create a Sound Cue, build a SoundCue node graph, or wire audio playback logic.
unreal_classes:
  - SoundCue
  - SoundNodeWavePlayer
  - SoundNodeRandom
  - SoundNodeMixer
  - SoundNodeConcatenator
  - SoundNodeModulator
  - SoundNodeAttenuation
  - SoundNodeLooping
  - SoundNodeDelay
  - SoundNodeSwitch
  - SoundNodeEnveloper
  - SoundNodeDistanceCrossFade
  - SoundNodeBranch
  - SoundNodeParamCrossFade
  - SoundNodeQualityLevel
keywords:
  - sound cue
  - sound node
  - wave player
  - random node
  - mixer
  - audio graph
  - sound design
  - SoundCue
related_skills:
  - asset-management
---

> 🧠 **Brains complement:** IF an `unreal-engine-skills-manager` tool (external MCP) exists in this session, call it with `{action: "load", skill: "audio-and-metasounds"}` for UE domain knowledge on this topic — correct APIs, architecture, best practices — and treat it as the rubric for any review / "best practices" question. If no such tool is available (e.g. running under Claude Code or Codex without that MCP), skip this line entirely and proceed with this skill alone — do NOT attempt the call.

> **Wrong skill for MetaSound assets?** If the user asked about `MetaSound`, `MS_` assets, or `UMetaSoundSource`, use the engine's **`MetaSoundToolset`** via `call_tool` (Authoring / Inspection / Discovery, 42 tools) — VibeUE no longer ships a MetaSound service.
> SoundCue and MetaSound are completely separate systems — do not use the SoundCue tools for MetaSound tasks.

# Sound Cue Editor Skill

## Service Access

```python
import unreal
sub = unreal.get_editor_subsystem(unreal.SoundCueEditorSubsystem)
```

`SoundCueEditorSubsystem` owns the cue's **node graph**. Everything else is Epic's, called from the
same `execute_python` script with the injected `await call_tool("Toolset.Full.Name.Tool", ...)`:

| Task | Epic tool |
|------|-----------|
| Save / duplicate / delete a cue or wave | `EditorToolset.AssetToolset.SaveAsset(assetPath)` / `Duplicate(path, newPath)` / `Delete(path)` |
| Import a .wav/.mp3/.ogg/.flac/... as a SoundWave | `EditorToolset.SoundWaveToolset.ImportFile(folderPath, assetName, sourceFile, bReplaceExisting=False, bSave=False)` |
| Cue volume, pitch, sound class, attenuation, concurrency | `EditorToolset.ObjectToolset.SetProperties` / `GetProperties` on the `USoundCue` |
| SoundWave looping, volume, pitch, subtitle priority | `EditorToolset.ObjectToolset.SetProperties` / `GetProperties` on the `USoundWave` |

Graph edits only dirty the package — save with `AssetToolset.SaveAsset` when done.

---

## Audio Flow Direction

**Audio flows FROM leaves TOWARD root.**

```
[WavePlayer] ──> [Random] ──> [Mixer] ──> [ROOT]
                                ^
                    [WavePlayer2] ──────┘
```

- Native `unreal.SoundCueEditorSubsystem.connect_nodes` connects graph nodes; `set_root_node` selects the output.

---

## Critical Rules

### ⚠️ Bool Properties — `b` prefix stripped in Python

UE strips the `b` prefix from bool UPROPERTY names in Python:

| C++ / UPROPERTY | Python attribute |
|-----------------|-----------------|
| `bSuccess`      | `success`       |
| `bIsRootNode`   | `is_root_node`  |
| `bLooping`      | `looping`       |
| `bStreaming`    | `streaming`     |

(Epic's `ObjectToolset` takes the **C++** names — `bLooping`, not `looping`.)

### ⚠️ Node indices are positional and may shift

`describe_sound_cue(cue).nodes` and `get_nodes(cue)` list nodes in graph order — indices can change if new nodes are added.
Always re-read them to get current indices before using the index-based calls below.
Native `SoundCueEditorSubsystem` calls take node objects, not these indices.

### ⚠️ Quality Level node has a fixed slot count

The Quality Level node input count is governed by the project’s **audio quality levels** (`AudioQualitySettings`). Confirm multiple levels exist before wiring multiple slots.

### ⚠️ New-cue defaults: `volume_multiplier` is `0.75`, not `1.0`

A freshly created empty cue reports `volume_multiplier = 0.75` (the engine `USoundCue` default),
`pitch_multiplier = 1.0`. Do **not** assert `1.0` for volume on a new cue — read it back instead.

### ⚠️ Connecting nodes

Use native `unreal.SoundCueEditorSubsystem.connect_nodes`; inspect its signature before wiring.

---

## Workflows

### Create and edit a SoundCue

Use native `unreal.SoundCueEditorSubsystem.create_sound_cue`, `add_node`, `add_node_input`, `connect_nodes`, and `set_root_node` to build the graph; save with `EditorToolset.AssetToolset.SaveAsset`. Inspect the native signatures first.

Use `ObjectToolset.SetProperties` / `GetProperties` for cue-level volume, pitch, sound class, attenuation and concurrency settings.
```

---

## API Reference

### Asset Lifecycle

Native asset lifecycle: `unreal.SoundCueEditorSubsystem.create_sound_cue` / `describe_sound_cue`, and `EditorToolset.AssetToolset.SaveAsset` / `Duplicate` / `Delete`.

### Node Creation

Use native `unreal.SoundCueEditorSubsystem.add_node` with a concrete node class and `add_node_input` for additional inputs.

### Node Management

| Task | Call |
|------|------|
| List nodes (current indices for the calls below) | `unreal.SoundCueEditorSubsystem.describe_sound_cue(cue)` |
| Reassign the wave on a Wave Player | `wp = sub.get_nodes(cue)[i]`, then `wp.set_editor_property('sound_wave_asset_ptr', wave)` inside a `unreal.ScopedEditorTransaction` (fallback: `ObjectToolset.SetProperties` with `{"SoundWaveAssetPtr": "<wave path>"}`) |
| Reposition nodes | `sub.layout_sound_cue(cue)` (whole graph; there is no per-node position call) |
| Read any node UPROPERTY | `{str(k): v for k, v in d.nodes[i].properties.items()}[prop]`, or `ObjectToolset.GetProperties` on `d.nodes[i].node` |
| Write any node UPROPERTY | `node = sub.get_nodes(cue)[i]`; in a `unreal.ScopedEditorTransaction`: `cue.modify()`, `node.set_editor_property(prop, value)`; then `cue.mark_package_dirty()` (or `ObjectToolset.SetProperties`) |

Native graph management: `unreal.SoundCueEditorSubsystem.describe_sound_cue`, `connect_nodes`, `disconnect_child`, `set_root_node`, and `remove_node`.

### Cue-Level Settings

Native cue properties: `ObjectToolset.SetProperties` / `GetProperties`; supply the complete desired concurrency set when replacing it.

### `describe_sound_cue` fields

```python
d = unreal.SoundCueEditorSubsystem.describe_sound_cue(cue)
d.root                  # the root node
d.nodes[i].node         # the SoundNode object
d.nodes[i].title        # display name
d.nodes[i].position     # graph position
d.nodes[i].children     # connected children
d.nodes[i].properties   # node UPROPERTY values
d.comments              # graph comments
```

### SoundWave Utilities

| Task | Call |
|------|------|
| Duration, sample rate, channels, looping, streaming | `ObjectToolset.GetProperties(instance={'refPath': wave_path}, properties=['Duration','NumChannels','SampleRate','bLooping','bStreaming'])` |

Native import: `SoundWaveToolset.ImportFile` (await completion; explicitly set `bReplaceExisting=true` and `bSave=true`).

Set a loaded SoundWave property with `sw.set_editor_property(prop, value)` (or ObjectToolset.SetProperties). Common C++ property names (use reflected Python names and typed values in Python):

| UPROPERTY (C++ name) | Type | Example value |
|---|---|---|
| `bLooping` | bool | `true` |
| `Volume` | float | `1.5` (ClampMin 0) |
| `Pitch` | float | `0.9` (0.125 – 4.0) |
| `SubtitlePriority` | float | `100.0` |

USoundWave has **no** `VolumeMultiplier` / `PitchMultiplier` (those are `USoundCue`'s). `bStreaming`
is a legacy bare `UPROPERTY()` — not editable, so `SetProperties` cannot set it; streaming is
controlled by the wave's `LoadingBehavior`.

### FSoundCueResult fields

```python
r.success      # bool
r.asset_path   # str
r.message      # str — human-readable status or error
```

## Sample scripts (run via `execute_python`)

- **`scripts/create_sound_cue.txt`** — create a SoundCue and add a delay node.

## Reference pages

Open these with the paths below; UE rewrites them to absolute paths when it serves this skill.

- [scripts/create_sound_cue.txt](scripts/create_sound_cue.txt) - runnable sample
