// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UTransactionService.generated.h"

/**
 * Transaction Service - stepping and clearing the editor's undo buffer (issue #469).
 *
 * Cut to the delta on the 5-9 branch. Use the engine for everything else:
 *  - grouping several edits into one named undo step is native Python:
 *    `with unreal.ScopedEditorTransaction("..."):` (PythonScriptPlugin)
 *  - asking what is IN the buffer -- the recent transactions, what each one touched, how recent a
 *    given one is: `EditorToolset.UndoHistoryToolset` (GetRecentTransactions /
 *    GetTransactionDetails / GetTransactionRecencyIndex / SearchTransactions), via call_tool
 *
 * Undo and buffer reset are native Python: `unreal.ToolsetLibrary.undo_transaction(True)` steps one
 * undo and returns False when nothing is left to undo (loop it to step several), and
 * `unreal.ToolsetTestLibrary.reset_transaction_buffer()` clears the buffer.
 *
 * Most VibeUE edits already open their own transaction, so an undo typically reverts the last whole
 * operation; wrap a batch in unreal.ScopedEditorTransaction to make it one step.
 *
 * Python:
 *   with unreal.ScopedEditorTransaction("Build wall"):
 *       pass  # ... spawn/modify actors ...
 *   unreal.ToolsetLibrary.undo_transaction(True)
 *   unreal.ToolsetTestLibrary.reset_transaction_buffer()  # clears HISTORY only; does NOT touch the world
 */
UCLASS(BlueprintType)
class VIBEUE_API UTransactionService : public UToolsetDefinition
{
	GENERATED_BODY()

public:

};
