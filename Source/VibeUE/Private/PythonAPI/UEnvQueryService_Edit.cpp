// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "PythonAPI/UEnvQueryService.h"

#if WITH_VIBEUE_EQS

#include "AIGraphNode.h"
#include "AIGraphTypes.h"
#include "DataProviders/AIDataProvider.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EnvQueryServiceInternal.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQueryGraph.h"
#include "EnvironmentQueryGraphNode.h"
#include "EnvironmentQueryGraphNode_Option.h"
#include "EnvironmentQueryGraphNode_Test.h"
#include "UObject/UnrealType.h"

/**
 * Structural writes to an Environment Query's editor graph.
 *
 * ===========================================================================================
 *  Why nothing here calls BreakAllNodeLinks / DestroyNode / RemoveNode(bBreakAllLinks = true)
 * ===========================================================================================
 *
 * UAIGraphNode::NodeConnectionListChanged() is not a deferred notification. It is:
 *
 *     void UAIGraphNode::NodeConnectionListChanged()
 *     {
 *         Super::NodeConnectionListChanged();
 *         GetAIGraph()->UpdateAsset();          // AIGraphNode.cpp:236-241
 *     }
 *
 * — a synchronous regeneration of UEnvQuery::Options from whatever the graph looks like at that
 * instant. UEdGraphNode::BreakAllNodeLinks() fires it on every node that lost a link, and
 * UEdGraph::RemoveNode() calls BreakAllNodeLinks() by default (EdGraph.cpp:261-281), as does
 * UEdGraphNode::DestroyNode().
 *
 * That matters because UpdateAsset opens with GetOptionsMutable().Reset() and ends in
 * RemoveOrphanedNodes(), which marks every no-longer-referenced UEnvQueryOption / UEnvQueryTest
 * RF_Transient and renames it into the transient package. Run it against a half-finished edit and
 * the loss is already permanent — before CommitGraph, and outside the discard guard, which brackets
 * only its own UpdateAsset() call and can neither see nor undo a regeneration that already
 * happened.
 *
 * The engine's pin-level API does exactly what is needed and notifies nothing:
 *
 *   - UEdGraphPin::MakeLinkTo / BreakAllPinLinks(bNotifyNodes = false) mutate the two LinkedTo
 *     arrays and stop (EdGraphPin.cpp:514, 705). UEnvironmentQueryGraph::SpawnMissingNodes links
 *     its own spawned option nodes with MakeLinkTo for the same reason (:442-445).
 *   - UEdGraph::AddNode and UEdGraph::RemoveNode(..., bBreakAllLinks = false) broadcast
 *     OnGraphChanged — an editor-UI signal with no listeners headlessly — and never touch
 *     UpdateAsset.
 *
 * So every function below rewires pins directly and lets CommitGraph perform the single
 * regeneration, once, over a graph that is valid again.
 *
 * ===========================================================================================
 *  ...and why nothing here calls UAIGraphNode::AddSubNode either
 * ===========================================================================================
 *
 * The same hazard, one class up. The engine attaches a test with
 * `OptionNode->AddSubNode(TestNode, Graph)` (EnvironmentQueryGraph.cpp:463), and AddSubNode ends:
 *
 *     SubNodes.Add(SubNode);
 *     OnSubNodeAdded(SubNode);
 *     ParentGraph->NotifyGraphChanged();
 *     GetAIGraph()->UpdateAsset();          // AIGraphNode.cpp:294-298
 *
 * — the same synchronous regeneration, before CommitGraph, outside the discard guard and outside the
 * sticky-poison check, on a graph that is mid-edit by construction. It also cannot take an index:
 * it always appends, so it could not implement AddTest's Index argument anyway.
 *
 * What the rest of that API does in 5.8, verified rather than assumed, because the BehaviorTree
 * counterparts differ:
 *
 *   - UAIGraphNode::OnSubNodeAdded / OnSubNodeRemoved are empty in the base class
 *     (AIGraphNode.cpp:301-304, 318-321) and UEnvironmentQueryGraphNode_Option overrides NEITHER, so
 *     neither notifies anything here. (UBehaviorTreeGraphNode overrides both.)
 *   - UAIGraphNode::InsertSubNodeAt(SubNode, DropIndex) is `SubNodes.Insert` for DropIndex > -1 and
 *     `SubNodes.Add` otherwise (AIGraphNode.cpp:330-340) — safe, and no packed index: the
 *     three-field packing is UBehaviorTreeGraphNode's override, and EQS has no override at all.
 *   - UAIGraphNode::RemoveSubNode is Modify() + RemoveSingle + OnSubNodeRemoved (:306-312) — also
 *     safe, but it does not clear ParentNode and finds its target by value rather than by the index
 *     the caller resolved.
 *
 * The array is manipulated directly below regardless: it is exactly what those two would do, it
 * cannot be changed out from under this code by a future override, and it lets a resolved index be
 * used as an index.
 *
 * Sub-nodes are deliberately NOT added to Graph->Nodes, and the reason is not the one the sibling
 * BehaviorTree plan records. UEnvironmentQueryGraph::UpdateAsset does not clear ParentNode for
 * entries in Nodes — it never touches ParentNode except to ASSIGN it from a parent option
 * (EnvironmentQueryGraph.cpp:87, :250), verified by grepping the whole file. What is true here is
 * simpler: the engine never lists a sub-node in Nodes (SpawnMissingSubNodes attaches through
 * SubNodes alone, EnvironmentQueryGraph.cpp:449-466), UAIGraph::CollectAllNodeInstances already
 * reaches every sub-node's instance through its parent's SubNodes array (AIGraph.cpp:197-203) so
 * listing it buys no orphan protection, and Nodes is what the graph editor draws — an entry there is
 * a free-floating node a human would see beside the option it is supposed to be inside.
 *
 * ===========================================================================================
 *  Order is position, position is link order
 * ===========================================================================================
 *
 * UpdateAsset sorts the root's links by node X position (FCompareNodeXLocation,
 * EnvironmentQueryGraph.cpp:65) and writes the options out in that order, so option order is node X
 * position and nothing else. CommitGraph runs VibeEQS::ArrangeGraph first, which lays the root's
 * links out left to right in *link* order — which is what turns the LinkedTo array manipulated here
 * into the order the query actually runs. Insert at index 0 without that pass and the new option is
 * merely present; with it, it is first.
 *
 * ===========================================================================================
 *  What a failed commit leaves behind
 * ===========================================================================================
 *
 * Each function refuses up front for every reason it can detect (unknown class, unresolvable path,
 * a removal that would empty the query), so by the time it mutates, the only way the commit fails
 * is a locked graph or a failed package save. The in-memory graph then holds the edit while the
 * file does not, and it is not rolled back: nothing was written, the next successful commit writes
 * it, and reopening the asset (OpenWriteGuard -> Initialize -> SpawnMissingNodes) reconciles the two
 * from UEnvQuery::Options. Recorded rather than papered over — an atomic rollback would need to
 * restore link ORDER as well as membership, and an untested rollback path is a worse hazard than a
 * documented one.
 */
namespace VibeEQSEdit
{
	/**
	 * AddOption and AddTest return a path on success, so their failures need a prefix a caller can
	 * test. Every other method here returns empty on success and the reason otherwise, so it needs
	 * no marker.
	 */
	FString OptionError(const FString& Reason)
	{
		return FString::Printf(TEXT("ERROR: %s"), *Reason);
	}

	/** "Option[1]/@test[0]" — the path GetQuery reports for a test and every test method accepts. */
	FString MakeTestPath(const FString& OptionPath, int32 TestIndex)
	{
		return FString::Printf(TEXT("%s/@test[%d]"), *OptionPath, TestIndex);
	}

	/**
	 * The option segment of a test path — "Option[1]" from "Option[1]/@test[0]". Empty if there is
	 * no '/' separator at all, which is how "this is an option path, not a test path" is detected
	 * without duplicating the path grammar ResolveNodePath owns.
	 */
	FString OptionSegmentOf(const FString& TestPath)
	{
		FString OptionSegment;
		FString TestSegment;
		return TestPath.Split(TEXT("/"), &OptionSegment, &TestSegment) ? OptionSegment : FString();
	}

	/**
	 * Resolve a test path to the option node that owns the sub-node AND the index it sits at.
	 *
	 * Both halves are needed by every mutator below, and neither can be derived safely from the
	 * other: the index is what the path means, and the owning option is what the index is an index
	 * into. ParentNode is deliberately not trusted as the route to the option — UpdateAsset assigns
	 * it (EnvironmentQueryGraph.cpp:87) but only for sub-nodes of root-linked options carrying a
	 * generator, so on a graph mid-repair it can be stale or null while the path is still perfectly
	 * well defined.
	 *
	 * False with OutError set for every failure. Resolution goes through VibeEQS::ResolveNodePath so
	 * the grammar, the range checks and the rejection of trailing junk all live in exactly one place.
	 */
	bool ResolveTestLocation(UEnvironmentQueryGraph* Graph, const FString& TestPath,
		const FString& AssetPath, UEnvironmentQueryGraphNode_Option*& OutOptionNode,
		int32& OutTestIndex, FString& OutError)
	{
		OutOptionNode = nullptr;
		OutTestIndex = INDEX_NONE;

		UEnvironmentQueryGraphNode* Node = VibeEQS::ResolveNodePath(Graph, TestPath);
		if (!Node)
		{
			OutError = FString::Printf(
				TEXT("No test at path '%s' in %s. Test paths are index-based and zero-based within "
					 "their option, as in 'Option[0]/@test[1]'; call GetQuery to list what is there."),
				*TestPath, *AssetPath);
			return false;
		}

		const FString OptionPath = OptionSegmentOf(TestPath);
		if (OptionPath.IsEmpty())
		{
			// The path resolved, to an option. Distinguished from "nothing there" for the same reason
			// ResolveOptionNode distinguishes the mirror case: it is the difference between a typo and
			// a caller addressing the wrong kind of node.
			OutError = FString::Printf(
				TEXT("'%s' in %s is an option path, not a test path. This call takes a test path such "
					 "as 'Option[0]/@test[0]'."),
				*TestPath, *AssetPath);
			return false;
		}

		UEnvironmentQueryGraphNode_Option* OptionNode =
			Cast<UEnvironmentQueryGraphNode_Option>(VibeEQS::ResolveNodePath(Graph, OptionPath));
		const int32 TestIndex = OptionNode ? OptionNode->SubNodes.IndexOfByKey(Node) : INDEX_NONE;
		if (TestIndex == INDEX_NONE)
		{
			// Unreachable through ResolveNodePath, which found this very node in that very array a
			// few lines ago. Reported rather than checked away: if it ever fires, the path grammar and
			// the graph have stopped agreeing, and mutating an index derived from that would edit
			// whichever sub-node happens to sit there.
			OutError = FString::Printf(
				TEXT("'%s' in %s resolved to a node its own option does not list as a sub-node"),
				*TestPath, *AssetPath);
			return false;
		}

		OutOptionNode = OptionNode;
		OutTestIndex = TestIndex;
		return true;
	}

	/** The option node at OptionPath, or nullptr with OutError set. */
	UEnvironmentQueryGraphNode_Option* ResolveOptionNode(UEnvironmentQueryGraph* Graph,
		const FString& OptionPath, const FString& AssetPath, FString& OutError)
	{
		UEnvironmentQueryGraphNode* Node = VibeEQS::ResolveNodePath(Graph, OptionPath);
		if (!Node)
		{
			OutError = FString::Printf(
				TEXT("No node at path '%s' in %s. Option paths are index-based and zero-based, as in "
					 "'Option[0]'; call GetQuery to list what is there."),
				*OptionPath, *AssetPath);
			return nullptr;
		}

		UEnvironmentQueryGraphNode_Option* OptionNode = Cast<UEnvironmentQueryGraphNode_Option>(Node);
		if (!OptionNode)
		{
			// Distinct from "nothing there": the path resolved, to a test sub-node. Saying so is the
			// difference between a typo and a caller addressing the wrong kind of node.
			OutError = FString::Printf(
				TEXT("'%s' in %s is not an option (it resolved to a test sub-node). This call takes an "
					 "option path such as 'Option[0]'."),
				*OptionPath, *AssetPath);
			return nullptr;
		}

		return OptionNode;
	}

	/**
	 * Move OptionNode's link so it becomes the TargetIndex'th option the root feeds.
	 *
	 * Operates on the *slots* the option links occupy in RootOut->LinkedTo rather than on the array
	 * as a whole, so anything else linked to that pin — which nothing here creates, but a
	 * hand-edited asset can carry — keeps its absolute position instead of being reordered or
	 * dropped by an edit that was about options.
	 *
	 * Reordering LinkedTo in place is safe: a link is reciprocal membership in two arrays, and
	 * neither the order of either array nor UEdGraphPin's own invariants depend on where in it a
	 * given entry sits. UpdateAsset re-sorts this same array itself.
	 */
	bool ReorderRootOptionLink(UEdGraphPin* RootOut, const UEnvironmentQueryGraphNode_Option* OptionNode,
		int32 TargetIndex)
	{
		if (!RootOut || !OptionNode)
		{
			return false;
		}

		TArray<int32> Slots;
		TArray<UEdGraphPin*> OptionLinks;
		int32 CurrentIndex = INDEX_NONE;
		for (int32 LinkIndex = 0; LinkIndex < RootOut->LinkedTo.Num(); ++LinkIndex)
		{
			UEdGraphPin* Linked = RootOut->LinkedTo[LinkIndex];
			const UEdGraphNode* Owner = Linked ? Linked->GetOwningNode() : nullptr;
			if (!Owner || !Owner->IsA<UEnvironmentQueryGraphNode_Option>())
			{
				continue;
			}

			if (Owner == OptionNode)
			{
				// A duplicated link would make "the" position ambiguous; GetOptionNodes reports such
				// a node once, so the first slot is the one that position refers to.
				if (CurrentIndex != INDEX_NONE)
				{
					continue;
				}
				CurrentIndex = OptionLinks.Num();
			}

			Slots.Add(LinkIndex);
			OptionLinks.Add(Linked);
		}

		if (CurrentIndex == INDEX_NONE)
		{
			return false;
		}

		const int32 ClampedTarget = FMath::Clamp(TargetIndex, 0, OptionLinks.Num() - 1);
		if (ClampedTarget == CurrentIndex)
		{
			return true;
		}

		UEdGraphPin* Moved = OptionLinks[CurrentIndex];
		OptionLinks.RemoveAt(CurrentIndex);
		OptionLinks.Insert(Moved, ClampedTarget);

		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			RootOut->LinkedTo[Slots[Index]] = OptionLinks[Index];
		}
		return true;
	}
}

using namespace VibeEQSEdit;

DEFINE_LOG_CATEGORY_STATIC(LogVibeEQSRepair, Log, All);

#else  // WITH_VIBEUE_EQS

namespace
{
	const TCHAR* const GEQSEditUnavailable =
		TEXT("EQS authoring is unavailable: the EnvironmentQueryEditor plugin is not enabled in this "
			 "build (WITH_VIBEUE_EQS=0).");
}

#endif // WITH_VIBEUE_EQS
