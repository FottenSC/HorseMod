#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector2D -FallbackName=Vector2D
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=ScalarParameterValue -FallbackName=ScalarParameterValue
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=TextureParameterValue -FallbackName=TextureParameterValue
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=VectorParameterValue -FallbackName=VectorParameterValue
#include "ELuxTexturePrinter.h"
#include "LuxTexturePrintParam.generated.h"

class UMaterialInterface;

USTRUCT(BlueprintType)
struct FLuxTexturePrintParam {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    ELuxTexturePrinter PrinterType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    FVector2D Dimensions;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<FScalarParameterValue> ScalarParams;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<FVectorParameterValue> VectorParams;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<FTextureParameterValue> TextureParams;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UMaterialInterface* PrinterMaterialOverride;
    
    LUXORGAME_API FLuxTexturePrintParam();
};

