#pragma once
#include "CoreMinimal.h"
#include "ExpressionInput.h"
#include "MaterialExpression.h"
#include "MaterialExpressionBlackBody.generated.h"

UCLASS(Blueprintable)
class UMaterialExpressionBlackBody : public UMaterialExpression {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    FExpressionInput Temp;
    
    UMaterialExpressionBlackBody();

};

