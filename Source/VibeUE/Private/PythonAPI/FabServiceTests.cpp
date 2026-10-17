// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Fab/FabLibraryClient.h"
#include "Fab/FabManifestClient.h"
#include "Fab/FabEndpoints.h"
#include "Fab/FabAuthBridge.h"
#include "Fab/FabCatalogClient.h"
#include "Fab/FabBrowserBridge.h"
#include "PythonAPI/UFabService.h"
#include "Json.h"

// WITH_VIBEUE_FAB: the client implementations these tests link against are compiled out when the
// engine install lacks the Fab plugin (issue #525) — see VibeUE.Build.cs.
#if WITH_AUTOMATION_TESTS && WITH_VIBEUE_FAB

// Pure discovery logic (engine-version compat, type routing, version rollup) is factored onto
// FFabLibraryAsset so it can be verified headlessly, with no editor/auth/network. Test path prefix
// VibeUE.Fab.*. Live auth + library + import are exercised by test_prompts/fab/fab_tests.md.

namespace
{
	FFabProjectVersion MakePV(const TArray<FString>& Engines)
	{
		FFabProjectVersion PV;
		PV.EngineVersions = Engines;
		return PV;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabSupportsEngineTest, "VibeUE.Fab.Compat.SupportsEngine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabSupportsEngineTest::RunTest(const FString&)
{
	FFabLibraryAsset A;
	A.ProjectVersions.Add(MakePV({TEXT("5.7"), TEXT("5.8")}));

	TestTrue(TEXT("exact 5.8 supported"), A.SupportsEngine(TEXT("5.8")));
	TestTrue(TEXT("5.7 supported"), A.SupportsEngine(TEXT("5.7")));
	TestFalse(TEXT("5.9 not supported"), A.SupportsEngine(TEXT("5.9")));
	TestTrue(TEXT("empty query = compatible"), A.SupportsEngine(TEXT("")));

	// Lenient suffix match: a "UE_5.8" token should satisfy a "5.8" query.
	FFabLibraryAsset B;
	B.ProjectVersions.Add(MakePV({TEXT("UE_5.8")}));
	TestTrue(TEXT("UE_5.8 satisfies 5.8"), B.SupportsEngine(TEXT("5.8")));

	// No projectVersions → unknown → treated as compatible (don't hide items on sparse data).
	FFabLibraryAsset C;
	TestTrue(TEXT("no versions = compatible (unknown)"), C.SupportsEngine(TEXT("5.8")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabAuthConsistencyTest, "VibeUE.Fab.Auth.StatusMatchesCredentials",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabAuthConsistencyTest::RunTest(const FString&)
{
	TSharedPtr<FJsonObject> Status;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(UFabService::AuthStatus(1.0f));
	if (!TestTrue(TEXT("auth status is JSON"), FJsonSerializer::Deserialize(Reader, Status) && Status.IsValid()))
	{
		return false;
	}
	bool bAuthenticated = false;
	if (Status->TryGetBoolField(TEXT("authenticated"), bAuthenticated) && bAuthenticated)
	{
		TestTrue(TEXT("authenticated means live EOS login"), FVibeFabAuth::IsLoggedIn());
		TestTrue(TEXT("authenticated means account available"), !FVibeFabAuth::GetEpicAccountId().IsEmpty());
		TestTrue(TEXT("authenticated means usable access token"), !FVibeFabAuth::GetAccessToken().IsEmpty());
	}
	TestFalse(TEXT("status never returns an access token"), Status->HasField(TEXT("access_token")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabDeriveTypeTest, "VibeUE.Fab.Compat.DeriveAssetType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabDeriveTypeTest::RunTest(const FString&)
{
	{
		FFabLibraryAsset A; A.Source = TEXT("quixel");
		TestEqual(TEXT("quixel source"), A.DeriveAssetType(), TEXT("quixel"));
	}
	{
		FFabLibraryAsset A; A.Source = TEXT("megascans");
		TestEqual(TEXT("megascans source"), A.DeriveAssetType(), TEXT("quixel"));
	}
	{
		FFabLibraryAsset A; A.DistributionMethod = TEXT("engine_plugin");
		TestEqual(TEXT("plugin dist"), A.DeriveAssetType(), TEXT("plugin"));
	}
	{
		FFabLibraryAsset A; A.DistributionMethod = TEXT("asset_pack");
		TestEqual(TEXT("asset pack dist"), A.DeriveAssetType(), TEXT("unreal-engine"));
	}
	{
		FFabLibraryAsset A; A.DistributionMethod = TEXT("complete_project");
		TestEqual(TEXT("complete project dist"), A.DeriveAssetType(), TEXT("complete-project"));
	}
	{
		FFabLibraryAsset A;   // nothing set → generic model
		TestEqual(TEXT("default = model"), A.DeriveAssetType(), TEXT("model"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabAllEngineVersionsTest, "VibeUE.Fab.Compat.AllEngineVersions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabAllEngineVersionsTest::RunTest(const FString&)
{
	FFabLibraryAsset A;
	A.ProjectVersions.Add(MakePV({TEXT("5.7"), TEXT("5.8")}));
	A.ProjectVersions.Add(MakePV({TEXT("5.8"), TEXT("5.6")}));   // 5.8 duplicated across versions

	const TArray<FString> All = A.AllEngineVersions();
	TestEqual(TEXT("de-duplicated count"), All.Num(), 3);
	TestTrue(TEXT("has 5.6"), All.Contains(TEXT("5.6")));
	TestTrue(TEXT("has 5.7"), All.Contains(TEXT("5.7")));
	TestTrue(TEXT("has 5.8"), All.Contains(TEXT("5.8")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabFreeCatalogEndpointsTest, "VibeUE.Fab.FreeCatalog.Endpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabFreeCatalogEndpointsTest::RunTest(const FString&)
{
	const FString Search = VibeUE::Fab::CatalogSearchUrl(TEXT("https://www.fab.com"),
		TEXT("forest rock"), TEXT("Quixel Megascans"), TEXT("next+page"));
	TestTrue(TEXT("search is constrained to free"), Search.Contains(TEXT("is_free=1")));
	TestTrue(TEXT("query is URL encoded"), Search.Contains(TEXT("q=forest%20rock")));
	TestTrue(TEXT("seller is URL encoded"), Search.Contains(TEXT("seller=Quixel%20Megascans")));
	TestTrue(TEXT("cursor is URL encoded"), Search.Contains(TEXT("cursor=next%2Bpage")));

	const FString Download = VibeUE::Fab::CatalogDownloadInfoUrl(TEXT("https://www.fab.com"),
		TEXT("listing-id"), TEXT("gltf"), TEXT("file-id"), TEXT("Windows"));
	TestEqual(TEXT("download-info endpoint"), Download,
		TEXT("https://www.fab.com/i/listings/listing-id/asset-formats/gltf/files/file-id/download-info?platform=Windows"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabFreeImportEulaGuardTest, "VibeUE.Fab.FreeCatalog.EulaGuard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabFreeImportEulaGuardTest::RunTest(const FString&)
{
	const FString Response = UFabService::ImportFreeAsset(TEXT("test-listing"), TEXT("High"),
		TEXT("gltf"), TEXT("personal"), false);
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response);
	TestTrue(TEXT("guard returns JSON"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid());
	if (Root.IsValid())
	{
		TestFalse(TEXT("import is refused without acceptance"), Root->GetBoolField(TEXT("success")));
		TestEqual(TEXT("guard error code"), Root->GetStringField(TEXT("error_code")),
			TEXT("EULA_ACCEPTANCE_REQUIRED"));
		TestFalse(TEXT("library remains unchanged"), Root->GetBoolField(TEXT("library_changed")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabNativeGuardsTest, "VibeUE.Fab.OwnedSource.Guards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabNativeGuardsTest::RunTest(const FString&)
{
	TestEqual(TEXT("native cursor encoded"), VibeUE::Fab::NativeLibraryPath(TEXT("a+b&x=1")),
		TEXT("/i/library/search?count=100&sort_by=-createdAt&cursor=a%2Bb%26x%3D1"));
	FString Body, Error;
	TestFalse(TEXT("external URL refused"), VibeUE::Fab::BrowserGet(TEXT("https://example.com/"), Body, Error));
	TestFalse(TEXT("traversal refused"), VibeUE::Fab::BrowserGet(TEXT("/i/listings/../accounts"), Body, Error));
	FFabFreeDownload Download;
	TestFalse(TEXT("malformed listing refused"), FVibeFabCatalog::ResolveOwnedDownload(TEXT("https://www.fab.com"),
		TEXT("not-a-uuid"), TEXT("Medium"), TEXT("gltf"), Download, Error));
	TestFalse(TEXT("unsupported format refused"), FVibeFabCatalog::ResolveOwnedDownload(TEXT("https://www.fab.com"),
		TEXT("db8c5bda-e867-412a-8e9a-f27c536d4ff8"), TEXT("Medium"), TEXT("fbx"), Download, Error));
	UFabBrowserResponse* Response = NewObject<UFabBrowserResponse>();
	Response->RequestId = TEXT("expected");
	Response->Complete(TEXT("wrong"), 200, TEXT("{}"));
	TestFalse(TEXT("wrong request callback ignored"), Response->bDone);
	Response->Complete(TEXT("expected"), 403, TEXT("private response must not be returned"));
	TestTrue(TEXT("HTTP rejection completes request"), Response->bDone);
	TestTrue(TEXT("HTTP rejection withholds body"), Response->Body.IsEmpty());
	TestFalse(TEXT("HTTP rejection reports error"), Response->Error.IsEmpty());
	UFabBrowserResponse* WrongAccount = NewObject<UFabBrowserResponse>();
	WrongAccount->RequestId = TEXT("account-test");
	WrongAccount->ExpectedAccount = TEXT("not-the-current-account");
	WrongAccount->Complete(TEXT("account-test"), 200, TEXT("must-not-leak"));
	TestTrue(TEXT("account mismatch withholds body"), WrongAccount->Body.IsEmpty());
	TestFalse(TEXT("account mismatch reports error"), WrongAccount->Error.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabManifestResponseTest, "VibeUE.Fab.Manifest.ParseResponse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabManifestResponseTest::RunTest(const FString&)
{
	const FString Response = TEXT("{\"downloadInfo\":[{\"type\":\"manifest\",\"assetFormat\":\"asset-format/game-engine/unreal-engine\",")
		TEXT("\"buildVersion\":\"UE_5.8\",\"distributionPoints\":[{\"manifestUrl\":")
		TEXT("\"https://cdn.invalid/file.manifest?X-Amz-Credential=secret\"}],")
		TEXT("\"distributionPointBaseUrls\":[\"https://cdn.invalid/Chunks\"]}]}");
	FFabDownloadInfo Info;
	FString Error;
	TestTrue(TEXT("valid response parses"), FVibeFabManifest::ParseDownloadResponse(Response, Info, Error));
	TestTrue(TEXT("response is BuildPatch"), Info.bIsBuildPatch);
	TestEqual(TEXT("build version"), Info.BuildVersion, TEXT("UE_5.8"));
	TestEqual(TEXT("one base location"), Info.BaseUrls.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabManifestMalformedTest, "VibeUE.Fab.Manifest.MalformedData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabManifestMalformedTest::RunTest(const FString&)
{
	FFabDownloadInfo Info;
	FString Error;
	int32 HttpCode = 0;
	TestFalse(TEXT("malformed response rejected"),
		FVibeFabManifest::ParseDownloadResponse(TEXT("{not-json"), Info, Error));
	TestFalse(TEXT("missing artifact fields rejected without network"),
		FVibeFabManifest::Fetch(TEXT("https://www.fab.com"), TEXT(""), TEXT(""), TEXT(""),
			TEXT("Windows"), TEXT("unused"), 1.0, Info, HttpCode, Error));

	const TArray<uint8> InvalidManifest = {0x01, 0x02, 0x03};
	FFabManifestInspection Inspection;
	TestFalse(TEXT("malformed BuildPatch manifest rejected"),
		FVibeFabManifest::ParseBuildPatchData(InvalidManifest, 0, Inspection, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeFabManifestSanitizationTest, "VibeUE.Fab.Manifest.SanitizedProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeFabManifestSanitizationTest::RunTest(const FString&)
{
	FFabManifestInspection Inspection;
	Inspection.ManifestBytes = 256;
	Inspection.BuildSizeBytes = 1024;
	Inspection.DownloadSizeBytes = 512;
	Inspection.TotalFiles = 1;
	FFabManifestInspection::FFile File;
	File.Path = TEXT("Content/Trees/SM_English_Oak.uasset");
	File.SizeBytes = 1024;
	File.TypeHint = TEXT("unreal-package");
	Inspection.Files.Add(MoveTemp(File));

	const TSharedPtr<FJsonObject> Object = Inspection.ToJsonObject();
	TestEqual(TEXT("manifest fidelity is explicit"), Object->GetStringField(TEXT("metadata_fidelity")), TEXT("manifest"));
	TestEqual(TEXT("payload request count is zero"), Object->GetNumberField(TEXT("payload_chunk_requests")), 0.0);
	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
	TestFalse(TEXT("signed URL is absent"), Json.Contains(TEXT("https://")));
	TestFalse(TEXT("credential marker is absent"), Json.Contains(TEXT("X-Amz")));
	TestFalse(TEXT("token field is absent"), Json.Contains(TEXT("token"), ESearchCase::IgnoreCase));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeAssetRegistryMissingPackageTest, "VibeUE.Fab.AssetRegistry.MissingPackage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVibeAssetRegistryMissingPackageTest::RunTest(const FString&)
{
	const FString Result = UFabService::InspectAssetRegistryPackage(TEXT("/Game/DefinitelyMissing/VibeUECatalogTest"));
	TSharedPtr<FJsonObject> Object;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Result);
	TestTrue(TEXT("result is JSON"), FJsonSerializer::Deserialize(Reader, Object));
	TestFalse(TEXT("missing package fails"), Object->GetBoolField(TEXT("success")));
	TestEqual(TEXT("missing package is classified"), Object->GetStringField(TEXT("error_code")),
		TEXT("ASSET_REGISTRY_NOT_FOUND"));
	return true;
}

#endif // WITH_AUTOMATION_TESTS && WITH_VIBEUE_FAB
