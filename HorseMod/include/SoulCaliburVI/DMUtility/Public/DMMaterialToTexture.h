#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "DMMaterialToTexture.generated.h"

class UCanvas;
class UCanvasRenderTarget2D;
class UMaterialInterface;
class UTexture;
class UTextureRenderTarget2D;

UCLASS(Blueprintable)
class DMUTILITY_API UDMMaterialToTexture : public UObject {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UCanvasRenderTarget2D* TempRT;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UTexture* TempResolved;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UMaterialInterface* TempMat;
    
public:
    UDMMaterialToTexture();

    UFUNCTION(BlueprintCallable)
    static UTexture* RequestTexture(UMaterialInterface* inMat, float InWidth, float inHeight, UTexture* inResolveTex);
    
    UFUNCTION(BlueprintCallable)
    static UTexture* RequestCapture(UMaterialInterface* inMat, float InWidth, float inHeight, UTextureRenderTarget2D* inRT);
    
protected:
    UFUNCTION(BlueprintCallable)
    void OnReceiveUpdate(UCanvas* Canvas, int32 Width, int32 Height);
    
};

