// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "PythonAPI/ULandscapeService.h"
#include "Components/SplineMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeEditLayer.h"
#include "LandscapeSplineControlPoint.h"
#include "LandscapeSplineSegment.h"
#include "LandscapeSplinesComponent.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include <limits>

namespace
{
	struct FSplineFixture
	{
		ALandscape* Landscape;
		ULandscapeSplinesComponent* Component;
		FString Label;

		FSplineFixture()
		{
			Label = TEXT("VibeUESplineTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
			FActorSpawnParameters Params;
			Params.ObjectFlags = RF_Transient | RF_Transactional;
			Landscape = GEditor->GetEditorWorldContext().World()->SpawnActor<ALandscape>(Params);
			Landscape->SetActorLabel(Label);
			Landscape->SetLandscapeGuid(FGuid::NewGuid());
			Landscape->CreateLandscapeInfo();
			Landscape->CreateSplineComponent();
			Component = Landscape->GetSplinesComponent();
		}

		~FSplineFixture()
		{
			if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
			{
				Info->UnregisterActor(Landscape);
			}
			Landscape->Destroy();
		}

		ULandscapeSplineSegment* AddSegment()
		{
			ULandscapeSplineControlPoint* Start = NewObject<ULandscapeSplineControlPoint>(Component, NAME_None, RF_Transactional);
			ULandscapeSplineControlPoint* End = NewObject<ULandscapeSplineControlPoint>(Component, NAME_None, RF_Transactional);
			Start->Location = FVector(0, 0, 500);
			End->Location = FVector(2000, 600, 700);
			Start->Rotation = FRotator(5, 20, 3);
			End->Rotation = FRotator(-4, -15, 9);
			Start->Width = 200;
			End->Width = 400;
			Start->SideFalloff = 300;
			End->SideFalloff = 700;
			Start->EndFalloff = End->EndFalloff = 0;
			Start->SegmentMeshOffset = 2;
			End->SegmentMeshOffset = 6;
			Component->GetControlPoints().Add(Start);
			Component->GetControlPoints().Add(End);
			ULandscapeSplineSegment* Segment = NewObject<ULandscapeSplineSegment>(Component, NAME_None, RF_Transactional);
			Segment->Connections[0].ControlPoint = Start;
			Segment->Connections[0].TangentLen = 1500;
			Segment->Connections[1].ControlPoint = End;
			Segment->Connections[1].TangentLen = -1800;
			Start->ConnectedSegments.Add(FLandscapeSplineConnection(Segment, 0));
			End->ConnectedSegments.Add(FLandscapeSplineConnection(Segment, 1));
			Segment->bRaiseTerrain = Segment->bLowerTerrain = false;
			Segment->bPlaceSplineMeshesInStreamingLevels = false;
			FLandscapeSplineMeshEntry Entry;
			Entry.Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			Segment->SplineMeshes.Add(Entry);
			Component->GetSegments().Add(Segment);
			Segment->UpdateSplinePoints();
			return Segment;
		}

		int32 MeshCount() const
		{
			TArray<USplineMeshComponent*> Meshes;
			Landscape->GetComponents(Meshes);
			return Meshes.Num();
		}
	};

	const EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeLandscapeSplineDeletionTest, "VibeUE.Landscape.Splines.Deletion", Flags)
bool FVibeLandscapeSplineDeletionTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->PlayWorld)
	{
		AddError(TEXT("Requires an idle Editor world"));
		return false;
	}
	FSplineFixture Fixture;
	ULandscapeSplineSegment* Segment = Fixture.AddSegment();
	ULandscapeSplineControlPoint* Start = Segment->Connections[0].ControlPoint;
	ULandscapeSplineControlPoint* End = Segment->Connections[1].ControlPoint;
	TestTrue(TEXT("generated mesh fixture"), Fixture.MeshCount() > 0);
	AddExpectedError(TEXT("ULandscapeService::DeleteSplinePoint: Invalid index -1"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("invalid delete rejected"), ULandscapeService::DeleteSplinePoint(Fixture.Label, -1));
	TestEqual(TEXT("invalid delete preserves points"), Fixture.Component->GetControlPoints().Num(), 2);
	TestTrue(TEXT("delete endpoint"), ULandscapeService::DeleteSplinePoint(Fixture.Label, 0));
	TestEqual(TEXT("one surviving control point"), Fixture.Component->GetControlPoints().Num(), 1);
	TestEqual(TEXT("no segments"), Fixture.Component->GetSegments().Num(), 0);
	TestEqual(TEXT("no orphan generated meshes"), Fixture.MeshCount(), 0);
	TestEqual(TEXT("survivor has no stale connection"), End->ConnectedSegments.Num(), 0);
	TestEqual(TEXT("deleted point detached"), Start->GetOuter(), static_cast<UObject*>(GetTransientPackage()));
	TestEqual(TEXT("deleted segment detached"), Segment->GetOuter(), static_cast<UObject*>(GetTransientPackage()));
	TestTrue(TEXT("Undo delete"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo restores points"), Fixture.Component->GetControlPoints().Num(), 2);
	TestTrue(TEXT("explicit recovery preserves Redo"), ULandscapeService::RebuildSplines(Fixture.Label));
	TestTrue(TEXT("Redo delete"), GEditor->RedoTransaction());
	TestTrue(TEXT("recover Redo"), ULandscapeService::RebuildSplines(Fixture.Label));
	TestEqual(TEXT("Redo leaves no orphan meshes"), Fixture.MeshCount(), 0);
	TestTrue(TEXT("Undo again"), GEditor->UndoTransaction());
	TestTrue(TEXT("recover before clear-all"), ULandscapeService::RebuildSplines(Fixture.Label));
	TestTrue(TEXT("clear all"), ULandscapeService::DeleteAllSplines(Fixture.Label));
	TestEqual(TEXT("all points removed"), Fixture.Component->GetControlPoints().Num(), 0);
	TestEqual(TEXT("all generated meshes removed"), Fixture.MeshCount(), 0);
	TestTrue(TEXT("Undo clear-all"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo clear-all restores points"), Fixture.Component->GetControlPoints().Num(), 2);
	TestTrue(TEXT("recover clear-all Undo"), ULandscapeService::RebuildSplines(Fixture.Label));
	TestTrue(TEXT("Redo clear-all"), GEditor->RedoTransaction());
	TestTrue(TEXT("recover empty layer after Redo"), ULandscapeService::RebuildSplines(Fixture.Label));
	TestEqual(TEXT("Redo clear-all no points"), Fixture.Component->GetControlPoints().Num(), 0);
	TestEqual(TEXT("Redo clear-all no generated meshes"), Fixture.MeshCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeLandscapeSplineSplitTest, "VibeUE.Landscape.Splines.Split", Flags)
bool FVibeLandscapeSplineSplitTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->PlayWorld)
	{
		AddError(TEXT("Requires an idle Editor world"));
		return false;
	}
	FSplineFixture Fixture;
	ULandscapeSplineSegment* Segment = Fixture.AddSegment();
	ULandscapeSplineControlPoint* Start = Segment->Connections[0].ControlPoint;
	ULandscapeSplineControlPoint* End = Segment->Connections[1].ControlPoint;
	Segment->RandomSeed = 17;
	FSplineFixture Unrelated;
	ULandscapeSplineSegment* UnrelatedSegment = Unrelated.AddSegment();
	const FGuid UnrelatedKey = UnrelatedSegment->GetModificationKey();
	TestTrue(TEXT("target-only rebuild"), ULandscapeService::RebuildSplines(Fixture.Label));
	TestEqual(TEXT("other Landscape not rebuilt"), UnrelatedSegment->GetModificationKey(), UnrelatedKey);
	TestFalse(TEXT("missing rebuild target rejected"), ULandscapeService::RebuildSplines(TEXT("NoSuchLandscape_VibeUESplineTest")));
	Segment->LDMaxDrawDistance = 12345;
	Segment->VirtualTextureMainPassMaxDrawDistance = 9000;
	Segment->LayerName = TEXT("RoadTest");
	const FRotator StartRotation = Start->Rotation;
	const FRotator EndRotation = End->Rotation;
	TArray<FVector> Samples;
	for (int32 Index = 0; Index <= 20; ++Index)
	{
		Samples.Add(Segment->Evaluate(Index / 20.0f));
	}
	TestFalse(TEXT("missing target rejected"), ULandscapeService::SplitSplineSegment(TEXT("NoSuchLandscape_VibeUESplineTest"), 0, 0.5f).bSuccess);
	TestFalse(TEXT("missing segment rejected"), ULandscapeService::SplitSplineSegment(Fixture.Label, -1, 0.5f).bSuccess);
	for (float Fraction : { 0.0f, 1.0f, -1.0f, std::numeric_limits<float>::quiet_NaN() })
	{
		TestFalse(TEXT("invalid fraction rejected"), ULandscapeService::SplitSplineSegment(Fixture.Label, 0, Fraction).bSuccess);
	}
	Segment->Connections[1].SocketName = TEXT("UnsupportedSocket");
	TestFalse(TEXT("socket split rejected"), ULandscapeService::SplitSplineSegment(Fixture.Label, 0, 0.4f).bSuccess);
	Segment->Connections[1].SocketName = NAME_None;
	const FVector EndLocation = End->Location;
	const float StartLength = Segment->Connections[0].TangentLen;
	const float EndLength = Segment->Connections[1].TangentLen;
	End->Location = Start->Location;
	Segment->Connections[0].TangentLen = Segment->Connections[1].TangentLen = 0;
	TestFalse(TEXT("degenerate split rejected"), ULandscapeService::SplitSplineSegment(Fixture.Label, 0, 0.4f).bSuccess);
	End->Location = EndLocation;
	Segment->Connections[0].TangentLen = StartLength;
	Segment->Connections[1].TangentLen = EndLength;
	TestEqual(TEXT("rejections leave points unchanged"), Fixture.Component->GetControlPoints().Num(), 2);
	TestEqual(TEXT("rejections leave spans unchanged"), Fixture.Component->GetSegments().Num(), 1);
	const FSplineCreateResult Result = ULandscapeService::SplitSplineSegment(Fixture.Label, 0, 0.4f);
	if (!TestTrue(TEXT("split succeeds"), Result.bSuccess))
	{
		return false;
	}
	TestEqual(TEXT("new point appended"), Result.PointIndex, 2);
	ULandscapeSplineControlPoint* Point = Fixture.Component->GetControlPoints()[2];
	ULandscapeSplineSegment* Second = Fixture.Component->GetSegments()[1];
	TestEqual(TEXT("width interpolated"), Point->Width, 280.0f);
	TestEqual(TEXT("mesh offset interpolated"), Point->SegmentMeshOffset, 3.6f);
	TestEqual(TEXT("mesh seed retained"), Second->RandomSeed, 17);
	TestEqual(TEXT("draw distance retained"), Second->LDMaxDrawDistance, 12345.0f);
	TestEqual(TEXT("virtual texture distance retained"), Second->VirtualTextureMainPassMaxDrawDistance, 9000.0f);
	TestEqual(TEXT("layer retained"), Second->LayerName, FName(TEXT("RoadTest")));
	TestEqual(TEXT("source mesh retained"), Second->SplineMeshes[0].Mesh.Get(), Segment->SplineMeshes[0].Mesh.Get());
	TestEqual(TEXT("new point has two connections"), Point->ConnectedSegments.Num(), 2);
	TestEqual(TEXT("start rotation unchanged"), Start->Rotation, StartRotation);
	TestEqual(TEXT("end rotation unchanged"), End->Rotation, EndRotation);
	for (int32 Index = 0; Index <= 20; ++Index)
	{
		const float Fraction = Index / 20.0f;
		const FVector Actual = Fraction <= 0.4f ? Segment->Evaluate(Fraction / 0.4f) : Second->Evaluate((Fraction - 0.4f) / 0.6f);
		TestTrue(TEXT("curve preserved within 0.01cm"), Actual.Equals(Samples[Index], 0.01));
	}
	TestTrue(TEXT("Undo split"), GEditor->UndoTransaction());
	TestEqual(TEXT("Undo split restores spans"), Fixture.Component->GetSegments().Num(), 1);
	TestTrue(TEXT("recover Undo split"), ULandscapeService::RebuildSplines(Fixture.Label));
	TestTrue(TEXT("Redo split"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo split restores spans"), Fixture.Component->GetSegments().Num(), 2);
	TestTrue(TEXT("recover Redo split"), ULandscapeService::RebuildSplines(Fixture.Label));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeLandscapeSplineLayerTest, "VibeUE.Landscape.Splines.EditLayer", Flags)
bool FVibeLandscapeSplineLayerTest::RunTest(const FString&)
{
	if (!GEditor || GEditor->PlayWorld)
	{
		AddError(TEXT("Requires an idle Editor world"));
		return false;
	}
	FSplineFixture Fixture;
	FSplineFixture Other;
	const int32 OtherCount = Other.Landscape->GetEditLayers().Num();
	const int32 BaseIndex = Fixture.Landscape->CreateLayer(TEXT("Base"));
	const FGuid BaseGuid = Fixture.Landscape->GetEditLayer(BaseIndex)->GetGuid();
	const int32 InitialCount = Fixture.Landscape->GetEditLayers().Num();
	Fixture.Landscape->SetEditingLayer(BaseGuid);
	TestEqual(TEXT("missing target rejected"), ULandscapeService::EnsureSplineEditLayer(TEXT("NoSuchLandscape_VibeUESplineTest"), TEXT("Road")), INDEX_NONE);
	TestEqual(TEXT("name collision rejected"), ULandscapeService::EnsureSplineEditLayer(Fixture.Label, TEXT("Base")), INDEX_NONE);
	TestEqual(TEXT("rejection preserves layers"), Fixture.Landscape->GetEditLayers().Num(), InitialCount);
	const int32 Index = ULandscapeService::EnsureSplineEditLayer(Fixture.Label, TEXT("Road"));
	if (!TestTrue(TEXT("spline layer created"), Index != INDEX_NONE)) { return false; }
	TestTrue(TEXT("native specialized class"), Fixture.Landscape->GetEditLayer(Index)->IsA<ULandscapeEditLayerSplines>());
	TestEqual(TEXT("base layer preserved"), Fixture.Landscape->GetEditLayer(BaseIndex)->GetGuid(), BaseGuid);
	TestEqual(TEXT("active base remains active"), Fixture.Landscape->GetEditingLayer(), BaseGuid);
	const FGuid SplineGuid = Fixture.Landscape->GetEditLayer(Index)->GetGuid();
	TestEqual(TEXT("repeat returns existing layer"), ULandscapeService::EnsureSplineEditLayer(Fixture.Label, TEXT("Road")), Index);
	TestEqual(TEXT("different requested name does not duplicate or rename"), ULandscapeService::EnsureSplineEditLayer(Fixture.Label, TEXT("AnotherName")), Index);
	TestEqual(TEXT("only one specialized layer"), Fixture.Landscape->GetEditLayers().Num(), InitialCount + 1);
	TestEqual(TEXT("other Landscape untouched"), Other.Landscape->GetEditLayers().Num(), OtherCount);
	TestTrue(TEXT("Undo creation"), GEditor->UndoTransaction());
	TestNull(TEXT("Undo removes specialized layer"), Fixture.Landscape->FindEditLayerOfType(ULandscapeEditLayerSplines::StaticClass()));
	TestTrue(TEXT("Redo creation"), GEditor->RedoTransaction());
	TestEqual(TEXT("Redo restores same layer"), Fixture.Landscape->GetEditLayer(Index)->GetGuid(), SplineGuid);
	return true;
}

#endif
