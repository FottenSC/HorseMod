#pragma once
#include "CoreMinimal.h"
#include "CaptureRequest.generated.h"

class UMaterialInterface;
class UObject;
class UTextureRenderTarget2D;

USTRUCT(BlueprintType)
struct DMUTILITY_API FCaptureRequest {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UTextureRenderTarget2D* RT;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UMaterialInterface* Mat;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<UObject*> tmpObject;
    
    FCaptureRequest();
};

