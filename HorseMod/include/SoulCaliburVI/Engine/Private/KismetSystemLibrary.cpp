#include "KismetSystemLibrary.h"
#include "Templates/SubclassOf.h"

UKismetSystemLibrary::UKismetSystemLibrary() {
}

void UKismetSystemLibrary::UnregisterForRemoteNotifications() {
}

void UKismetSystemLibrary::UnloadPrimaryAssetList(const TArray<FPrimaryAssetId>& PrimaryAssetIdList) {
}

void UKismetSystemLibrary::UnloadPrimaryAsset(FPrimaryAssetId PrimaryAssetId) {
}

void UKismetSystemLibrary::StackTrace() {
}

bool UKismetSystemLibrary::SphereTraceSingleForObjects(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::SphereTraceSingleByProfile(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, FName ProfileName, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::SphereTraceSingle(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, TEnumAsByte<ETraceTypeQuery> TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::SphereTraceMultiForObjects(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::SphereTraceMultiByProfile(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, FName ProfileName, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::SphereTraceMulti(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, TEnumAsByte<ETraceTypeQuery> TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::SphereOverlapComponents(UObject* WorldContextObject, const FVector SpherePos, float SphereRadius, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, UClass* ComponentClassFilter, const TArray<AActor*>& ActorsToIgnore, TArray<UPrimitiveComponent*>& OutComponents) {
    return false;
}

bool UKismetSystemLibrary::SphereOverlapActors(UObject* WorldContextObject, const FVector SpherePos, float SphereRadius, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, UClass* ActorClassFilter, const TArray<AActor*>& ActorsToIgnore, TArray<AActor*>& OutActors) {
    return false;
}

void UKismetSystemLibrary::ShowPlatformSpecificLeaderboardScreen(const FString& CategoryName) {
}

void UKismetSystemLibrary::ShowPlatformSpecificAchievementsScreen(APlayerController* SpecificPlayer) {
}

void UKismetSystemLibrary::ShowInterstitialAd() {
}

void UKismetSystemLibrary::ShowAdBanner(int32 AdIdIndex, bool bShowOnBottomOfScreen) {
}

void UKismetSystemLibrary::SetWindowTitle(const FText& Title) {
}

void UKismetSystemLibrary::SetVolumeButtonsHandledBySystem(bool bEnabled) {
}

void UKismetSystemLibrary::SetVectorPropertyByName(UObject* Object, FName PropertyName, const FVector& Value) {
}

void UKismetSystemLibrary::SetUserActivity(const FUserActivity& UserActivity) {
}

void UKismetSystemLibrary::SetTransformPropertyByName(UObject* Object, FName PropertyName, const FTransform& Value) {
}

void UKismetSystemLibrary::SetTextPropertyByName(UObject* Object, FName PropertyName, const FText& Value) {
}

void UKismetSystemLibrary::SetSuppressViewportTransitionMessage(UObject* WorldContextObject, bool bState) {
}

void UKismetSystemLibrary::SetStructurePropertyByName(UObject* Object, FName PropertyName, const FGenericStruct& Value) {
}

void UKismetSystemLibrary::SetStringPropertyByName(UObject* Object, FName PropertyName, const FString& Value) {
}

void UKismetSystemLibrary::SetRotatorPropertyByName(UObject* Object, FName PropertyName, const FRotator& Value) {
}

void UKismetSystemLibrary::SetObjectPropertyByName(UObject* Object, FName PropertyName, UObject* Value) {
}

void UKismetSystemLibrary::SetNamePropertyByName(UObject* Object, FName PropertyName, const FName& Value) {
}

void UKismetSystemLibrary::SetLinearColorPropertyByName(UObject* Object, FName PropertyName, const FLinearColor& Value) {
}

void UKismetSystemLibrary::SetIntPropertyByName(UObject* Object, FName PropertyName, int32 Value) {
}

void UKismetSystemLibrary::SetInterfacePropertyByName(UObject* Object, FName PropertyName, const FScriptInterface& Value) {
}

void UKismetSystemLibrary::SetFloatPropertyByName(UObject* Object, FName PropertyName, float Value) {
}

void UKismetSystemLibrary::SetCollisionProfileNameProperty(UObject* Object, FName PropertyName, const FCollisionProfileName& Value) {
}

void UKismetSystemLibrary::SetClassPropertyByName(UObject* Object, FName PropertyName, UClass* Value) {
}

void UKismetSystemLibrary::SetBytePropertyByName(UObject* Object, FName PropertyName, uint8 Value) {
}

void UKismetSystemLibrary::SetBoolPropertyByName(UObject* Object, FName PropertyName, bool Value) {
}

void UKismetSystemLibrary::SetAssetPropertyByName(UObject* Object, FName PropertyName, const UObject*& Value) {
}

void UKismetSystemLibrary::SetAssetClassPropertyByName(UObject* Object, FName PropertyName, const UClass*& Value) {
}

void UKismetSystemLibrary::RetriggerableDelay(UObject* WorldContextObject, float Duration, FLatentActionInfo LatentInfo) {
}

void UKismetSystemLibrary::ResetGamepadAssignmentToController(int32 ControllerId) {
}

void UKismetSystemLibrary::ResetGamepadAssignments() {
}

void UKismetSystemLibrary::RegisterForRemoteNotifications() {
}

void UKismetSystemLibrary::QuitGame(UObject* WorldContextObject, APlayerController* SpecificPlayer, TEnumAsByte<EQuitPreference::Type> QuitPreference) {
}

void UKismetSystemLibrary::PrintWarning(const FString& inString) {
}

void UKismetSystemLibrary::PrintText(UObject* WorldContextObject, const FText InText, bool bPrintToScreen, bool bPrintToLog, FLinearColor TextColor, float Duration) {
}

void UKismetSystemLibrary::PrintString(UObject* WorldContextObject, const FString& inString, bool bPrintToScreen, bool bPrintToLog, FLinearColor TextColor, float Duration) {
}

bool UKismetSystemLibrary::NotEqual_SoftObjectReference(const UObject*& A, const UObject*& B) {
    return false;
}

bool UKismetSystemLibrary::NotEqual_SoftClassReference(const UClass*& A, const UClass*& B) {
    return false;
}

bool UKismetSystemLibrary::NotEqual_PrimaryAssetType(FPrimaryAssetType A, FPrimaryAssetType B) {
    return false;
}

bool UKismetSystemLibrary::NotEqual_PrimaryAssetId(FPrimaryAssetId A, FPrimaryAssetId B) {
    return false;
}

void UKismetSystemLibrary::MoveComponentTo(USceneComponent* Component, FVector TargetRelativeLocation, FRotator TargetRelativeRotation, bool bEaseOut, bool bEaseIn, float OverTime, bool bForceShortestRotationPath, TEnumAsByte<EMoveComponentAction::Type> MoveAction, FLatentActionInfo LatentInfo) {
}

FText UKismetSystemLibrary::MakeLiteralText(FText Value) {
    return FText::GetEmpty();
}

FString UKismetSystemLibrary::MakeLiteralString(const FString& Value) {
    return TEXT("");
}

FName UKismetSystemLibrary::MakeLiteralName(FName Value) {
    return NAME_None;
}

int32 UKismetSystemLibrary::MakeLiteralInt(int32 Value) {
    return 0;
}

float UKismetSystemLibrary::MakeLiteralFloat(float Value) {
    return 0.0f;
}

uint8 UKismetSystemLibrary::MakeLiteralByte(uint8 Value) {
    return 0;
}

bool UKismetSystemLibrary::MakeLiteralBool(bool Value) {
    return false;
}

void UKismetSystemLibrary::LoadInterstitialAd(int32 AdIdIndex) {
}

void UKismetSystemLibrary::LoadAssetClass(UObject* WorldContextObject, UClass* AssetClass, UKismetSystemLibrary::FOnAssetClassLoaded OnLoaded, FLatentActionInfo LatentInfo) {
}

void UKismetSystemLibrary::LoadAsset(UObject* WorldContextObject, UObject* Asset, UKismetSystemLibrary::FOnAssetLoaded OnLoaded, FLatentActionInfo LatentInfo) {
}

bool UKismetSystemLibrary::LineTraceSingleForObjects(UObject* WorldContextObject, const FVector Start, const FVector End, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::LineTraceSingleByProfile(UObject* WorldContextObject, const FVector Start, const FVector End, FName ProfileName, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::LineTraceSingle(UObject* WorldContextObject, const FVector Start, const FVector End, TEnumAsByte<ETraceTypeQuery> TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::LineTraceMultiForObjects(UObject* WorldContextObject, const FVector Start, const FVector End, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::LineTraceMultiByProfile(UObject* WorldContextObject, const FVector Start, const FVector End, FName ProfileName, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::LineTraceMulti(UObject* WorldContextObject, const FVector Start, const FVector End, TEnumAsByte<ETraceTypeQuery> TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

void UKismetSystemLibrary::LaunchURL(const FString& URL) {
}

void UKismetSystemLibrary::K2_UnPauseTimerHandle(UObject* WorldContextObject, FTimerHandle Handle) {
}

void UKismetSystemLibrary::K2_UnPauseTimerDelegate(FTimerDynamicDelegate Delegate) {
}

void UKismetSystemLibrary::K2_UnPauseTimer(UObject* Object, const FString& FunctionName) {
}

bool UKismetSystemLibrary::K2_TimerExistsHandle(UObject* WorldContextObject, FTimerHandle Handle) {
    return false;
}

bool UKismetSystemLibrary::K2_TimerExistsDelegate(FTimerDynamicDelegate Delegate) {
    return false;
}

bool UKismetSystemLibrary::K2_TimerExists(UObject* Object, const FString& FunctionName) {
    return false;
}

FTimerHandle UKismetSystemLibrary::K2_SetTimerDelegate(FTimerDynamicDelegate Delegate, float Time, bool bLooping) {
    return FTimerHandle{};
}

FTimerHandle UKismetSystemLibrary::K2_SetTimer(UObject* Object, const FString& FunctionName, float Time, bool bLooping) {
    return FTimerHandle{};
}

void UKismetSystemLibrary::K2_PauseTimerHandle(UObject* WorldContextObject, FTimerHandle Handle) {
}

void UKismetSystemLibrary::K2_PauseTimerDelegate(FTimerDynamicDelegate Delegate) {
}

void UKismetSystemLibrary::K2_PauseTimer(UObject* Object, const FString& FunctionName) {
}

bool UKismetSystemLibrary::K2_IsValidTimerHandle(FTimerHandle Handle) {
    return false;
}

bool UKismetSystemLibrary::K2_IsTimerPausedHandle(UObject* WorldContextObject, FTimerHandle Handle) {
    return false;
}

bool UKismetSystemLibrary::K2_IsTimerPausedDelegate(FTimerDynamicDelegate Delegate) {
    return false;
}

bool UKismetSystemLibrary::K2_IsTimerPaused(UObject* Object, const FString& FunctionName) {
    return false;
}

bool UKismetSystemLibrary::K2_IsTimerActiveHandle(UObject* WorldContextObject, FTimerHandle Handle) {
    return false;
}

bool UKismetSystemLibrary::K2_IsTimerActiveDelegate(FTimerDynamicDelegate Delegate) {
    return false;
}

bool UKismetSystemLibrary::K2_IsTimerActive(UObject* Object, const FString& FunctionName) {
    return false;
}

FTimerHandle UKismetSystemLibrary::K2_InvalidateTimerHandle(FTimerHandle& Handle) {
    return FTimerHandle{};
}

float UKismetSystemLibrary::K2_GetTimerRemainingTimeHandle(UObject* WorldContextObject, FTimerHandle Handle) {
    return 0.0f;
}

float UKismetSystemLibrary::K2_GetTimerRemainingTimeDelegate(FTimerDynamicDelegate Delegate) {
    return 0.0f;
}

float UKismetSystemLibrary::K2_GetTimerRemainingTime(UObject* Object, const FString& FunctionName) {
    return 0.0f;
}

float UKismetSystemLibrary::K2_GetTimerElapsedTimeHandle(UObject* WorldContextObject, FTimerHandle Handle) {
    return 0.0f;
}

float UKismetSystemLibrary::K2_GetTimerElapsedTimeDelegate(FTimerDynamicDelegate Delegate) {
    return 0.0f;
}

float UKismetSystemLibrary::K2_GetTimerElapsedTime(UObject* Object, const FString& FunctionName) {
    return 0.0f;
}

void UKismetSystemLibrary::K2_ClearTimerHandle(UObject* WorldContextObject, FTimerHandle Handle) {
}

void UKismetSystemLibrary::K2_ClearTimerDelegate(FTimerDynamicDelegate Delegate) {
}

void UKismetSystemLibrary::K2_ClearTimer(UObject* Object, const FString& FunctionName) {
}

void UKismetSystemLibrary::K2_ClearAndInvalidateTimerHandle(UObject* WorldContextObject, FTimerHandle& Handle) {
}

bool UKismetSystemLibrary::IsValidSoftObjectReference(const UObject*& SoftObjectReference) {
    return false;
}

bool UKismetSystemLibrary::IsValidSoftClassReference(const UClass*& SoftClassReference) {
    return false;
}

bool UKismetSystemLibrary::IsValidPrimaryAssetType(FPrimaryAssetType PrimaryAssetType) {
    return false;
}

bool UKismetSystemLibrary::IsValidPrimaryAssetId(FPrimaryAssetId PrimaryAssetId) {
    return false;
}

bool UKismetSystemLibrary::IsValidClass(UClass* Class) {
    return false;
}

bool UKismetSystemLibrary::IsValid(const UObject* Object) {
    return false;
}

bool UKismetSystemLibrary::IsStandalone(UObject* WorldContextObject) {
    return false;
}

bool UKismetSystemLibrary::IsServer(UObject* WorldContextObject) {
    return false;
}

bool UKismetSystemLibrary::IsPackagedForDistribution() {
    return false;
}

bool UKismetSystemLibrary::IsLoggedIn(APlayerController* SpecificPlayer) {
    return false;
}

bool UKismetSystemLibrary::IsInterstitialAdRequested() {
    return false;
}

bool UKismetSystemLibrary::IsInterstitialAdAvailable() {
    return false;
}

bool UKismetSystemLibrary::IsDedicatedServer(UObject* WorldContextObject) {
    return false;
}

bool UKismetSystemLibrary::IsControllerAssignedToGamepad(int32 ControllerId) {
    return false;
}

void UKismetSystemLibrary::HideAdBanner() {
}

bool UKismetSystemLibrary::GetVolumeButtonsHandledBySystem() {
    return false;
}

FString UKismetSystemLibrary::GetUniqueDeviceId() {
    return TEXT("");
}

bool UKismetSystemLibrary::GetSupportedFullscreenResolutions(TArray<FIntPoint>& Resolutions) {
    return false;
}

UObject* UKismetSystemLibrary::GetSoftObjectReferenceFromPrimaryAssetId(FPrimaryAssetId PrimaryAssetId) {
    return NULL;
}

UClass* UKismetSystemLibrary::GetSoftClassReferenceFromPrimaryAssetId(FPrimaryAssetId PrimaryAssetId) {
    return NULL;
}

int32 UKismetSystemLibrary::GetRenderingMaterialQualityLevel() {
    return 0;
}

int32 UKismetSystemLibrary::GetRenderingDetailMode() {
    return 0;
}

void UKismetSystemLibrary::GetPrimaryAssetsWithBundleState(const TArray<FName>& RequiredBundles, const TArray<FName>& ExcludedBundles, const TArray<FPrimaryAssetType>& ValidTypes, bool bForceCurrentState, TArray<FPrimaryAssetId>& OutPrimaryAssetIdList) {
}

void UKismetSystemLibrary::GetPrimaryAssetIdList(FPrimaryAssetType PrimaryAssetType, TArray<FPrimaryAssetId>& OutPrimaryAssetIdList) {
}

FPrimaryAssetId UKismetSystemLibrary::GetPrimaryAssetIdFromSoftObjectReference(UObject* SoftObjectReference) {
    return FPrimaryAssetId{};
}

FPrimaryAssetId UKismetSystemLibrary::GetPrimaryAssetIdFromSoftClassReference(UClass* SoftClassReference) {
    return FPrimaryAssetId{};
}

FPrimaryAssetId UKismetSystemLibrary::GetPrimaryAssetIdFromObject(UObject* Object) {
    return FPrimaryAssetId{};
}

FPrimaryAssetId UKismetSystemLibrary::GetPrimaryAssetIdFromClass(UClass* Class) {
    return FPrimaryAssetId{};
}

TArray<FString> UKismetSystemLibrary::GetPreferredLanguages() {
    return TArray<FString>();
}

FString UKismetSystemLibrary::GetPlatformUserName() {
    return TEXT("");
}

FString UKismetSystemLibrary::GetPathName(const UObject* Object) {
    return TEXT("");
}

FString UKismetSystemLibrary::GetObjectName(const UObject* Object) {
    return TEXT("");
}

UObject* UKismetSystemLibrary::GetObjectFromPrimaryAssetId(FPrimaryAssetId PrimaryAssetId) {
    return NULL;
}

int32 UKismetSystemLibrary::GetMinYResolutionForUI() {
    return 0;
}

int32 UKismetSystemLibrary::GetMinYResolutionFor3DView() {
    return 0;
}

FString UKismetSystemLibrary::GetLocalCurrencySymbol() {
    return TEXT("");
}

FString UKismetSystemLibrary::GetLocalCurrencyCode() {
    return TEXT("");
}

float UKismetSystemLibrary::GetGameTimeInSeconds(UObject* WorldContextObject) {
    return 0.0f;
}

FString UKismetSystemLibrary::GetGameName() {
    return TEXT("");
}

FString UKismetSystemLibrary::GetGameBundleId() {
    return TEXT("");
}

FString UKismetSystemLibrary::GetEngineVersion() {
    return TEXT("");
}

FString UKismetSystemLibrary::GetDisplayName(const UObject* Object) {
    return TEXT("");
}

FString UKismetSystemLibrary::GetDeviceId() {
    return TEXT("");
}

FString UKismetSystemLibrary::GetDefaultLocale() {
    return TEXT("");
}

FString UKismetSystemLibrary::GetDefaultLanguage() {
    return TEXT("");
}

bool UKismetSystemLibrary::GetCurrentBundleState(FPrimaryAssetId PrimaryAssetId, bool bForceCurrentState, TArray<FName>& OutBundles) {
    return false;
}

bool UKismetSystemLibrary::GetConvenientWindowedResolutions(TArray<FIntPoint>& Resolutions) {
    return false;
}

void UKismetSystemLibrary::GetComponentBounds(const USceneComponent* Component, FVector& Origin, FVector& BoxExtent, float& SphereRadius) {
}

FString UKismetSystemLibrary::GetCommandLine() {
    return TEXT("");
}

UClass* UKismetSystemLibrary::GetClassFromPrimaryAssetId(FPrimaryAssetId PrimaryAssetId) {
    return NULL;
}

FString UKismetSystemLibrary::GetClassDisplayName(UClass* Class) {
    return TEXT("");
}

int32 UKismetSystemLibrary::GetAdIDCount() {
    return 0;
}

void UKismetSystemLibrary::GetActorListFromComponentList(const TArray<UPrimitiveComponent*>& ComponentList, UClass* ActorClassFilter, TArray<AActor*>& OutActorList) {
}

void UKismetSystemLibrary::GetActorBounds(const AActor* Actor, FVector& Origin, FVector& BoxExtent) {
}

void UKismetSystemLibrary::ForceCloseAdBanner() {
}

void UKismetSystemLibrary::FlushPersistentDebugLines(UObject* WorldContextObject) {
}

void UKismetSystemLibrary::FlushDebugStrings(UObject* WorldContextObject) {
}

void UKismetSystemLibrary::ExecuteConsoleCommand(UObject* WorldContextObject, const FString& Command, APlayerController* SpecificPlayer) {
}

bool UKismetSystemLibrary::EqualEqual_SoftObjectReference(const UObject*& A, const UObject*& B) {
    return false;
}

bool UKismetSystemLibrary::EqualEqual_SoftClassReference(const UClass*& A, const UClass*& B) {
    return false;
}

bool UKismetSystemLibrary::EqualEqual_PrimaryAssetType(FPrimaryAssetType A, FPrimaryAssetType B) {
    return false;
}

bool UKismetSystemLibrary::EqualEqual_PrimaryAssetId(FPrimaryAssetId A, FPrimaryAssetId B) {
    return false;
}

void UKismetSystemLibrary::DrawDebugString(UObject* WorldContextObject, const FVector TextLocation, const FString& Text, AActor* TestBaseActor, FLinearColor TextColor, float Duration) {
}

void UKismetSystemLibrary::DrawDebugSphere(UObject* WorldContextObject, const FVector Center, float Radius, int32 Segments, FLinearColor LineColor, float Duration, float Thickness) {
}

void UKismetSystemLibrary::DrawDebugPoint(UObject* WorldContextObject, const FVector Position, float Size, FLinearColor PointColor, float Duration) {
}

void UKismetSystemLibrary::DrawDebugPlane(UObject* WorldContextObject, const FPlane& PlaneCoordinates, const FVector Location, float Size, FLinearColor PlaneColor, float Duration) {
}

void UKismetSystemLibrary::DrawDebugLine(UObject* WorldContextObject, const FVector LineStart, const FVector LineEnd, FLinearColor LineColor, float Duration, float Thickness) {
}

void UKismetSystemLibrary::DrawDebugFrustum(UObject* WorldContextObject, const FTransform& FrustumTransform, FLinearColor FrustumColor, float Duration, float Thickness) {
}

void UKismetSystemLibrary::DrawDebugFloatHistoryTransform(UObject* WorldContextObject, const FDebugFloatHistory& FloatHistory, const FTransform& DrawTransform, FVector2D DrawSize, FLinearColor DrawColor, float Duration) {
}

void UKismetSystemLibrary::DrawDebugFloatHistoryLocation(UObject* WorldContextObject, const FDebugFloatHistory& FloatHistory, FVector DrawLocation, FVector2D DrawSize, FLinearColor DrawColor, float Duration) {
}

void UKismetSystemLibrary::DrawDebugCylinder(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, int32 Segments, FLinearColor LineColor, float Duration, float Thickness) {
}

void UKismetSystemLibrary::DrawDebugCoordinateSystem(UObject* WorldContextObject, const FVector AxisLoc, const FRotator AxisRot, float Scale, float Duration, float Thickness) {
}

void UKismetSystemLibrary::DrawDebugConeInDegrees(UObject* WorldContextObject, const FVector Origin, const FVector Direction, float Length, float AngleWidth, float AngleHeight, int32 NumSides, FLinearColor LineColor, float Duration, float Thickness) {
}

void UKismetSystemLibrary::DrawDebugCone(UObject* WorldContextObject, const FVector Origin, const FVector Direction, float Length, float AngleWidth, float AngleHeight, int32 NumSides, FLinearColor LineColor, float Duration, float Thickness) {
}

void UKismetSystemLibrary::DrawDebugCircle(UObject* WorldContextObject, FVector Center, float Radius, int32 NumSegments, FLinearColor LineColor, float Duration, float Thickness, FVector YAxis, FVector ZAxis, bool bDrawAxis) {
}

void UKismetSystemLibrary::DrawDebugCapsule(UObject* WorldContextObject, const FVector Center, float HalfHeight, float Radius, const FRotator Rotation, FLinearColor LineColor, float Duration, float Thickness) {
}

void UKismetSystemLibrary::DrawDebugCamera(const ACameraActor* CameraActor, FLinearColor CameraColor, float Duration) {
}

void UKismetSystemLibrary::DrawDebugBox(UObject* WorldContextObject, const FVector Center, FVector Extent, FLinearColor LineColor, const FRotator Rotation, float Duration, float Thickness) {
}

void UKismetSystemLibrary::DrawDebugArrow(UObject* WorldContextObject, const FVector LineStart, const FVector LineEnd, float ArrowSize, FLinearColor LineColor, float Duration, float Thickness) {
}

bool UKismetSystemLibrary::DoesImplementInterface(UObject* TestObject, TSubclassOf<UInterface> Interface) {
    return false;
}

void UKismetSystemLibrary::Delay(UObject* WorldContextObject, float Duration, FLatentActionInfo LatentInfo) {
}

void UKismetSystemLibrary::CreateCopyForUndoBuffer(UObject* ObjectToModify) {
}

UObject* UKismetSystemLibrary::Conv_SoftObjectReferenceToObject(const UObject*& Asset) {
    return NULL;
}

UClass* UKismetSystemLibrary::Conv_SoftClassReferenceToClass(const UClass*& AssetClass) {
    return NULL;
}

UObject* UKismetSystemLibrary::Conv_ObjectToSoftObjectReference(UObject* Object) {
    return NULL;
}

UObject* UKismetSystemLibrary::Conv_InterfaceToObject(const FScriptInterface& Interface) {
    return NULL;
}

UClass* UKismetSystemLibrary::Conv_ClassToSoftClassReference(const UClass*& Class) {
    return NULL;
}

void UKismetSystemLibrary::ControlScreensaver(bool bAllowScreenSaver) {
}

bool UKismetSystemLibrary::ComponentOverlapComponents(UPrimitiveComponent* Component, const FTransform& ComponentTransform, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, UClass* ComponentClassFilter, const TArray<AActor*>& ActorsToIgnore, TArray<UPrimitiveComponent*>& OutComponents) {
    return false;
}

bool UKismetSystemLibrary::ComponentOverlapActors(UPrimitiveComponent* Component, const FTransform& ComponentTransform, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, UClass* ActorClassFilter, const TArray<AActor*>& ActorsToIgnore, TArray<AActor*>& OutActors) {
    return false;
}

void UKismetSystemLibrary::CollectGarbage() {
}

bool UKismetSystemLibrary::CapsuleTraceSingleForObjects(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, float HalfHeight, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::CapsuleTraceSingleByProfile(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, float HalfHeight, FName ProfileName, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::CapsuleTraceSingle(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, float HalfHeight, TEnumAsByte<ETraceTypeQuery> TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::CapsuleTraceMultiForObjects(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, float HalfHeight, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::CapsuleTraceMultiByProfile(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, float HalfHeight, FName ProfileName, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::CapsuleTraceMulti(UObject* WorldContextObject, const FVector Start, const FVector End, float Radius, float HalfHeight, TEnumAsByte<ETraceTypeQuery> TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::CapsuleOverlapComponents(UObject* WorldContextObject, const FVector CapsulePos, float Radius, float HalfHeight, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, UClass* ComponentClassFilter, const TArray<AActor*>& ActorsToIgnore, TArray<UPrimitiveComponent*>& OutComponents) {
    return false;
}

bool UKismetSystemLibrary::CapsuleOverlapActors(UObject* WorldContextObject, const FVector CapsulePos, float Radius, float HalfHeight, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, UClass* ActorClassFilter, const TArray<AActor*>& ActorsToIgnore, TArray<AActor*>& OutActors) {
    return false;
}

bool UKismetSystemLibrary::CanLaunchURL(const FString& URL) {
    return false;
}

bool UKismetSystemLibrary::BoxTraceSingleForObjects(UObject* WorldContextObject, const FVector Start, const FVector End, const FVector HalfSize, const FRotator Orientation, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::BoxTraceSingleByProfile(UObject* WorldContextObject, const FVector Start, const FVector End, const FVector HalfSize, const FRotator Orientation, FName ProfileName, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::BoxTraceSingle(UObject* WorldContextObject, const FVector Start, const FVector End, const FVector HalfSize, const FRotator Orientation, TEnumAsByte<ETraceTypeQuery> TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, FHitResult& OutHit, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::BoxTraceMultiForObjects(UObject* WorldContextObject, const FVector Start, const FVector End, const FVector HalfSize, const FRotator Orientation, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::BoxTraceMultiByProfile(UObject* WorldContextObject, const FVector Start, const FVector End, FVector HalfSize, const FRotator Orientation, FName ProfileName, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::BoxTraceMulti(UObject* WorldContextObject, const FVector Start, const FVector End, FVector HalfSize, const FRotator Orientation, TEnumAsByte<ETraceTypeQuery> TraceChannel, bool bTraceComplex, const TArray<AActor*>& ActorsToIgnore, TEnumAsByte<EDrawDebugTrace::Type> DrawDebugType, TArray<FHitResult>& OutHits, bool bIgnoreSelf, FLinearColor TraceColor, FLinearColor TraceHitColor, float DrawTime) {
    return false;
}

bool UKismetSystemLibrary::BoxOverlapComponents(UObject* WorldContextObject, const FVector BoxPos, FVector Extent, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, UClass* ComponentClassFilter, const TArray<AActor*>& ActorsToIgnore, TArray<UPrimitiveComponent*>& OutComponents) {
    return false;
}

bool UKismetSystemLibrary::BoxOverlapActors(UObject* WorldContextObject, const FVector BoxPos, FVector BoxExtent, const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes, UClass* ActorClassFilter, const TArray<AActor*>& ActorsToIgnore, TArray<AActor*>& OutActors) {
    return false;
}

FDebugFloatHistory UKismetSystemLibrary::AddFloatHistorySample(float Value, const FDebugFloatHistory& FloatHistory) {
    return FDebugFloatHistory{};
}


