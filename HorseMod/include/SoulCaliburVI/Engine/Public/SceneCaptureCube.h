#pragma once
#include "CoreMinimal.h"
#include "SceneCapture.h"
#include "SceneCaptureCube.generated.h"

class UDrawFrustumComponent;
class USceneCaptureComponentCube;

UCLASS(Blueprintable)
class ENGINE_API ASceneCaptureCube : public ASceneCapture {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    USceneCaptureComponentCube* CaptureComponentCube;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UDrawFrustumComponent* DrawFrustum;
    
public:
    ASceneCaptureCube(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void OnInterpToggle(bool bEnable);
    
};

