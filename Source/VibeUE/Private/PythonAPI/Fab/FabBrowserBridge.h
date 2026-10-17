// Copyright Buckley Builds LLC 2026 All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FabBrowserBridge.generated.h"

class SWebBrowser;

/** A request-scoped callback, exposed only to the signed-in native Fab page. */
UCLASS()
class UFabBrowserResponse : public UObject
{
	GENERATED_BODY()
public:
	FString RequestId;
	FString Body;
	FString Error;
	FString ExpectedAccount;
	bool bDone = false;
	TWeakPtr<SWebBrowser> Browser;

	UFUNCTION()
	void Complete(const FString& Id, int32 Status, const FString& Json);
};

namespace VibeUE::Fab
{
	/** GET-only, fixed Fab routes. Returns false + pending until a later editor tick completes. */
	bool BrowserGet(const FString& Path, FString& OutBody, FString& OutError);
}
