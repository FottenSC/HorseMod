#pragma once
#include "CoreMinimal.h"
#include "EMaterialExpressionScreenPositionMapping.h"
#include "MaterialExpression.h"
#include "MaterialExpressionScreenPosition.generated.h"

UCLASS(Blueprintable, CollapseCategories)
class UMaterialExpressionScreenPosition : public UMaterialExpression {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EMaterialExpressionScreenPositionMapping> Mapping;
    
    UMaterialExpressionScreenPosition();

};

