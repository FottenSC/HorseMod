#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
#include "EUIControlEffectTarget.h"
#include "AnselFunctionLibrary.generated.h"

class UObject;

UCLASS(Blueprintable)
class ANSEL_API UAnselFunctionLibrary : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    UAnselFunctionLibrary();

    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static void StopSessionDev(UObject* WorldContextObject);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static void StopSession(UObject* WorldContextObject);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static void StartSessionDev(UObject* WorldContextObject);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static void StartSession(UObject* WorldContextObject);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static void SetUIControlVisibility(UObject* WorldContextObject, const TEnumAsByte<EUIControlEffectTarget> UIControlTarget, const bool bIsVisible);
    
    UFUNCTION(BlueprintCallable)
    static void SetSettleFrames(const int32 NumSettleFrames);
    
    UFUNCTION(BlueprintCallable)
    static void SetIsPhotographyAllowed(const bool bIsPhotographyAllowed);
    
    UFUNCTION(BlueprintCallable)
    static void SetCameraMovementSpeed(const float TranslationSpeed);
    
    UFUNCTION(BlueprintCallable)
    static void SetCameraConstraintDistance(const float MaxCameraDistance);
    
    UFUNCTION(BlueprintCallable)
    static void SetCameraConstraintCameraSize(const float CameraSize);
    
    UFUNCTION(BlueprintCallable)
    static void SetAutoPostprocess(const bool bShouldAutoPostprocess);
    
    UFUNCTION(BlueprintCallable)
    static void SetAutoPause(const bool bShouldAutoPause);
    
    UFUNCTION(BlueprintCallable)
    static bool IsPhotographyAvailable();
    
    UFUNCTION(BlueprintCallable)
    static bool IsPhotographyAllowed();
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static void ConstrainCameraByGeometry(UObject* WorldContextObject, const FVector NewCameraLocation, const FVector PreviousCameraLocation, const FVector OriginalCameraLocation, FVector& OutCameraLocation);
    
    UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
    static void ConstrainCameraByDistance(UObject* WorldContextObject, const FVector NewCameraLocation, const FVector PreviousCameraLocation, const FVector OriginalCameraLocation, FVector& OutCameraLocation, float MaxDistance);
    
};

