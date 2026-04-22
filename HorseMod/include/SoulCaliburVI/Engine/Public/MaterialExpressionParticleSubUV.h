#pragma once
#include "CoreMinimal.h"
#include "MaterialExpressionTextureSample.h"
#include "MaterialExpressionParticleSubUV.generated.h"

UCLASS(Blueprintable, CollapseCategories, MinimalAPI)
class UMaterialExpressionParticleSubUV : public UMaterialExpressionTextureSample {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bBlend: 1;
    
    UMaterialExpressionParticleSubUV();

};

