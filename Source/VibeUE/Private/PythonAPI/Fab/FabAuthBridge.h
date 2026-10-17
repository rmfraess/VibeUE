// Copyright Buckley Builds LLC 2026 All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * FabService auth reuses an active EOS platform when its token matches the Fab client, or creates a
 * fallback platform with the engine Fab plugin's baked credentials (read reflectively from
 * /Fab/Data/FabEos.FabEos) using PersistentAuth and the launcher exchange code. Native Fab sign-in
 * can recover an expired fallback session. Cached state alone never establishes usable credentials.
 * All EOS calls are compiled only under WITH_EOS_SDK (defined =1 by the EOSSDK module).
 */
class FVibeFabAuth
{
public:
	/** True on desktop editor builds where the EOS SDK is compiled in and initialized. */
	static bool IsSupported();

	/**
	 * Ensure a login is started and, pumping EOS for up to TimeoutSeconds, try to reach logged-in.
	 * Idempotent: creates the platform once, kicks login once, re-polls on later calls. Returns true
	 * once logged in. On any hard failure sets OutError and returns false.
	 */
	static bool EnsureLoggedIn(double TimeoutSeconds, FString& OutError);

	/** Current EOS login status for our platform. */
	static bool IsLoggedIn();

	/** The current EOS user access token (Bearer for fab.com), or empty if not logged in. */
	static FString GetAccessToken();

	/** The signed-in Epic account id as a string (used in the library URL path), or empty. */
	static FString GetEpicAccountId();

	/** "unsupported" | "idle" | "pending" | "logged_in" | "failed". */
	static FString GetStateString();

	/** Last hard error encountered (creds missing, SDK not initialized, login failed, …). */
	static FString GetLastError();
};
