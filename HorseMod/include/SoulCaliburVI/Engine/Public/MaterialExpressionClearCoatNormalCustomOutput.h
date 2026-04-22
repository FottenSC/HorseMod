#pragma once
#include "CoreMinimal.h"
#include "ExpressionInput.h"
#include "MaterialExpressionCustomOutput.h"
#include "MaterialExpressionClearCoatNormalCustomOutput.generated.h"

UCLASS(Blueprintable, CollapseCategories)
class UMaterialExpressionClearCoatNormalCustomOutput : public UMaterialExpressionCustomOutput {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    FExpressionInput Input;
    
    UMaterialExpressionClearCoatNormalCustomOutput();

};

