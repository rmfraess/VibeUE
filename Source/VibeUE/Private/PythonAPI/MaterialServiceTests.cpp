// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "PythonAPI/UMaterialService.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVibeMaterialInfoResultTest, "VibeUE.Materials.GetMaterialInfoResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVibeMaterialInfoResultTest::RunTest(const FString&)
{
	UFunction* Function = UMaterialService::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UMaterialService, GetMaterialInfoResult));
	TestNotNull(TEXT("Result operation is reflected"), Function);
	if (Function)
	{
		TestTrue(TEXT("Result operation is AICallable"), Function->HasMetaData(TEXT("AICallable")));
		const FStructProperty* ReturnProperty = CastField<FStructProperty>(Function->GetReturnProperty());
		TestTrue(TEXT("MCP receives the result struct as the direct return"),
			ReturnProperty && ReturnProperty->Struct == FMaterialInfoResult::StaticStruct());
		TestEqual(TEXT("Only the path and return value are parameters"), Function->NumParms, uint8(2));
	}

	const FString MaterialPath = TEXT("/Engine/EngineMaterials/DefaultMaterial");
	FMaterialDetailedInfo LegacyInfo;
	TestTrue(TEXT("Legacy C++ signature still works"), UMaterialService::GetMaterialInfo(MaterialPath, LegacyInfo));
	const FMaterialInfoResult Result = UMaterialService::GetMaterialInfoResult(MaterialPath);
	TestTrue(TEXT("Valid material succeeds"), Result.bSuccess);
	TestTrue(TEXT("Success has no error"), Result.ErrorMessage.IsEmpty());
	TestEqual(TEXT("Material identity is populated"), Result.Info.MaterialName, FString(TEXT("DefaultMaterial")));
	TestEqual(TEXT("Requested path is retained"), Result.Info.MaterialPath, MaterialPath);
	TestTrue(TEXT("Every detail matches the legacy read"),
		FMaterialDetailedInfo::StaticStruct()->CompareScriptStruct(&Result.Info, &LegacyInfo, 0));

	// LoadAsset intentionally logs errors for the empty and missing paths, not the wrong asset type.
	AddExpectedError(TEXT("LoadAsset failed:"), EAutomationExpectedErrorFlags::Contains, 2);
	const FString InvalidPaths[] = {
		TEXT(""),
		TEXT("/Engine/EngineMaterials/VibeUE_MissingMaterialInfo"),
		TEXT("/Engine/EngineResources/DefaultTexture")
	};
	const FMaterialDetailedInfo EmptyInfo;
	for (const FString& InvalidPath : InvalidPaths)
	{
		const FMaterialInfoResult Failure = UMaterialService::GetMaterialInfoResult(InvalidPath);
		TestFalse(FString::Printf(TEXT("Rejects '%s'"), *InvalidPath), Failure.bSuccess);
		TestFalse(TEXT("Failure has an explicit error"), Failure.ErrorMessage.IsEmpty());
		TestTrue(TEXT("Error includes the requested path"), Failure.ErrorMessage.Contains(InvalidPath));
		TestTrue(TEXT("Failure cannot expose stale successful details"),
			FMaterialDetailedInfo::StaticStruct()->CompareScriptStruct(&Failure.Info, &EmptyInfo, 0));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
