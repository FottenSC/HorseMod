#pragma once
#include "CoreMinimal.h"
#include "MaterialInstance.h"
#include "MaterialInstanceConstant.generated.h"

UCLASS(Blueprintable, CollapseCategories, MinimalAPI)
class UMaterialInstanceConstant : public UMaterialInstance {
    GENERATED_BODY()
public:
    UMaterialInstanceConstant();

};

