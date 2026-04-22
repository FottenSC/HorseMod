#pragma once
#include "CoreMinimal.h"
#include "FunctionExpressionInput.h"
#include "FunctionExpressionOutput.h"
#include "MaterialExpression.h"
#include "MaterialExpressionMaterialFunctionCall.generated.h"

class UMaterialFunction;

UCLASS(Blueprintable, MinimalAPI)
class UMaterialExpressionMaterialFunctionCall : public UMaterialExpression {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UMaterialFunction* MaterialFunction;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FFunctionExpressionInput> FunctionInputs;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FFunctionExpressionOutput> FunctionOutputs;
    
    UMaterialExpressionMaterialFunctionCall();

};

