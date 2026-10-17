// Copyright Buckley Builds LLC 2026 All Rights Reserved.
#include "FabBrowserBridge.h"
#include "FabAuthBridge.h"

#if WITH_VIBEUE_FAB
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "SWebBrowser.h"
#include "Layout/Children.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"

namespace
{
 struct FRequest
 {
  TStrongObjectPtr<UFabBrowserResponse> Response;
  double Started = 0;
 };
 TMap<FString, FRequest> Requests;

 TSharedPtr<SWebBrowser> FindFab(const TSharedRef<SWidget>& Widget)
 {
  if (Widget->GetType() == FName(TEXT("SWebBrowser")))
  {
   TSharedRef<SWebBrowser> Browser = StaticCastSharedRef<SWebBrowser>(Widget);
   const FString Url = Browser->GetUrl();
   if (Url.StartsWith(TEXT("https://www.fab.com/")) || Url.StartsWith(TEXT("https://fab.com/"))) return Browser;
  }
  if (FChildren* Children = Widget->GetChildren())
  {
   for (int32 Index = 0; Index < Children->Num(); ++Index)
   {
    if (TSharedPtr<SWebBrowser> Browser = FindFab(Children->GetChildAt(Index))) return Browser;
   }
  }
  return nullptr;
 }
 FString Quote(const FString& Value)
 {
  FString Json;
  TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
  Writer->WriteValue(Value); Writer->Close();
  return Json;
 }
}

void UFabBrowserResponse::Complete(const FString& Id, int32 Status, const FString& Json)
{
 if (bDone || Id != RequestId) return;
 bDone = true;
 if (Status == 200 && Json.Len() <= 8 * 1024 * 1024 && ExpectedAccount == FVibeFabAuth::GetEpicAccountId()) Body = Json;
 else Error = FString::Printf(TEXT("Native Fab request failed (HTTP %d)."), Status);
 if (Status == 409) Error = TEXT("Native Fab account differs from the active Epic account. Sign into the same account before retrying.");
 if (TSharedPtr<SWebBrowser> View = Browser.Pin()) View->UnbindUObject(TEXT("vibeuefabresponse"), this, false);
}

bool VibeUE::Fab::BrowserGet(const FString& Path, FString& OutBody, FString& OutError)
{
 // Never provide a general browser/credential proxy: only this service's read-only Fab routes.
 if (!(Path.StartsWith(TEXT("/i/library/")) || Path.StartsWith(TEXT("/i/listings/"))
  || Path.StartsWith(TEXT("/p/egl/listings/"))) || Path.Contains(TEXT(".."))
  || Path.Contains(TEXT("\\")) || Path.Contains(TEXT("#")))
 {
  OutError = TEXT("Unsupported native Fab read route."); return false;
 }
 if (FRequest* Request = Requests.Find(Path))
 {
  UFabBrowserResponse* Response = Request->Response.Get();
  if (!Response->bDone && FPlatformTime::Seconds() - Request->Started > 45)
  {
   Response->Complete(Response->RequestId, 0, TEXT(""));
  }
  if (!Response->bDone) { OutError = TEXT("NATIVE_REQUEST_PENDING"); return false; }
  OutBody = Response->Body; OutError = Response->Error;
  if (TSharedPtr<SWebBrowser> View = Response->Browser.Pin()) View->UnbindUObject(TEXT("vibeuefabresponse"), Response, false);
  Requests.Remove(Path);
  return OutError.IsEmpty();
 }
 for (auto It = Requests.CreateIterator(); It; ++It)
 {
  UFabBrowserResponse* Response = It.Value().Response.Get();
  if (Response->bDone || FPlatformTime::Seconds() - It.Value().Started > 45 || !Response->Browser.IsValid())
  {
   if (TSharedPtr<SWebBrowser> View = Response->Browser.Pin()) View->UnbindUObject(TEXT("vibeuefabresponse"), Response, false);
   It.RemoveCurrent();
  }
 }
 if (!FSlateApplication::IsInitialized()) { OutError = TEXT("Open the signed-in Fab window first."); return false; }
 TSharedPtr<SWebBrowser> Browser;
 TArray<TSharedRef<SWindow>> Windows;
 FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
 for (const TSharedRef<SWindow>& Window : Windows)
 {
  Browser = FindFab(Window);
  if (Browser) break;
 }
 if (!Browser) { OutError = TEXT("Open the signed-in Fab window first."); return false; }
 if (!Requests.IsEmpty()) { OutError = TEXT("NATIVE_REQUEST_PENDING"); return false; }
 FRequest Request;
 Request.Response.Reset(NewObject<UFabBrowserResponse>());
 Request.Started = FPlatformTime::Seconds();
 UFabBrowserResponse* Response = Request.Response.Get();
 Response->RequestId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
 Response->Browser = Browser;
 Response->ExpectedAccount = FVibeFabAuth::GetEpicAccountId();
 Browser->BindUObject(TEXT("vibeuefabresponse"), Response, false);
 const FString Script = FString::Printf(TEXT("(async()=>{const id=%s;try{const o={method:'GET',credentials:'same-origin',redirect:'error',headers:{Accept:'application/json'}};const me=await fetch('/i/users/me',o);if(!me.ok){await window.ue.vibeuefabresponse.complete(id,401,'');return;}const user=await me.json();if(user.epicId!==%s){await window.ue.vibeuefabresponse.complete(id,409,'');return;}const r=await fetch(%s,o);const text=await r.text();await window.ue.vibeuefabresponse.complete(id,r.status,text);}catch(e){await window.ue.vibeuefabresponse.complete(id,0,'');}})();"), *Quote(Response->RequestId), *Quote(Response->ExpectedAccount), *Quote(Path));
 Requests.Add(Path, MoveTemp(Request));
 Browser->ExecuteJavascript(Script);
 OutError = TEXT("NATIVE_REQUEST_PENDING");
 return false;
}
#else
void UFabBrowserResponse::Complete(const FString&, int32, const FString&) {}
#endif
