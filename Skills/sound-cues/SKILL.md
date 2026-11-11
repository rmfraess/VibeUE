---
name: sound-cues
display_name: Sound Cue Editor
description: Create and modify SoundCue assets — add/connect nodes (mixer, random, delay, attenuation, modulator, etc.) and set audio properties. Use when the user asks to create a Sound Cue, build a SoundCue node graph, or wire audio playback logic.
vibeue_classes:
  - SoundCueService
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
> SoundCue and MetaSound are completely separate systems — do not use `SoundCueService` for MetaSound tasks.

# Sound Cue Editor Skill

## Service Access

```python
import unreal
svc = unreal.SoundCueService()
```

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
| `bLooping`      | `is_looping`    |
| `bStreaming`    | `is_streaming`  |

### ⚠️ Node indices are positional and may shift

`list_nodes()` returns nodes in graph order — indices can change if new nodes are added.
Always call `list_nodes()` to get current indices before using the index-based calls below.
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

| Method | Returns | Notes |
|--------|---------|-------|
| `list_nodes(path)` | `Array[FSoundCueNodeInfo]` | current indices for the calls below |
| `set_wave_player_asset(path, index, wave_path)` | `bool` | reassign wave on existing node |
| `move_node(path, index, x, y)` | `bool` | reposition a node in the graph |
| `get_node_property(path, index, prop_name)` | `FString` | read any node UPROPERTY |
| `set_node_property(path, index, prop_name, value)` | `bool` | write any node UPROPERTY |

Native graph management: `unreal.SoundCueEditorSubsystem.describe_sound_cue`, `connect_nodes`, `disconnect_child`, `set_root_node`, and `remove_node`.

### Cue-Level Settings

Native cue properties: `ObjectToolset.SetProperties` / `GetProperties`; supply the complete desired concurrency set when replacing it.

### FSoundCueNodeInfo fields

```python
n.node_index      # int — graph node index
n.node_class      # str — e.g. "SoundNodeWavePlayer"
n.node_title      # str — display name
n.pos_x, n.pos_y  # float — graph position
n.is_root_node    # bool — True if this node is SoundCue.FirstNode
n.child_indices   # list[int] — indices of connected children (-1 = unconnected slot)
```

### SoundWave Utilities

| Method | Returns | Notes |
|--------|---------|-------|
| `get_sound_wave_info(sw_path)` | `FSoundWaveInfo` | duration, sample rate, channels, looping, streaming |

Native import: `SoundWaveToolset.ImportFile` (await completion; explicitly set `bReplaceExisting=true` and `bSave=true`).

Set a loaded SoundWave property with `sw.set_editor_property(prop, value)` (or ObjectToolset.SetProperties). Common C++ property names (use reflected Python names and typed values in Python):

| PropertyName | Type | Example value |
|---|---|---|
| `bLooping` | bool | `"true"` |
| `bStreaming` | bool | `"true"` |
| `VolumeMultiplier` | float | `"1.5"` |
| `PitchMultiplier` | float | `"0.9"` |
| `SubtitlePriority` | float | `"100.0"` |

### FSoundWaveInfo fields

```python
info.asset_path    # str
info.duration      # float — seconds
info.sample_rate   # int — Hz
info.num_channels  # int — 1=mono, 2=stereo
info.looping       # bool  (note: NOT is_looping — UE strips the 'b' only)
info.streaming     # bool  (note: NOT is_streaming)
```

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
