#pragma once
#include "CoreMinimal.h"
#include "ESamplerSourceMode.h"
#include "ETextureMipValueMode.h"
#include "ExpressionInput.h"
#include "MaterialExpressionTextureBase.h"
#include "MaterialExpressionTextureSample.generated.h"

UCLASS(Blueprintable, CollapseCategories)
class ENGINE_API UMaterialExpressionTextureSample : public UMaterialExpressionTextureBase {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    FExpressionInput Coordinates;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    FExpressionInput TextureObject;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    FExpressionInput MipValue;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    FExpressionInput CoordinatesDX;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    FExpressionInput CoordinatesDY;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ETextureMipValueMode> MipValueMode;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ESamplerSourceMode> SamplerSource;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 ConstCoordinate;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 ConstMipValue;
    
    UMaterialExpressionTextureSample();

};

