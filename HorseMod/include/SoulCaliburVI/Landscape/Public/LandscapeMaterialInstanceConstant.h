#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=MaterialInstanceConstant -FallbackName=MaterialInstanceConstant
#include "LandscapeMaterialInstanceConstant.generated.h"

UCLASS(Blueprintable, CollapseCategories, MinimalAPI)
class ULandscapeMaterialInstanceConstant : public UMaterialInstanceConstant {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bIsLayerThumbnail: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bDisableTessellation: 1;
    
    ULandscapeMaterialInstanceConstant();

};

