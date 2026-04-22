#pragma once
#include "CoreMinimal.h"
#include "ESpeedTreeGeometryType.h"
#include "ESpeedTreeLODType.h"
#include "ESpeedTreeWindType.h"
#include "MaterialExpression.h"
#include "MaterialExpressionSpeedTree.generated.h"

UCLASS(Blueprintable, CollapseCategories, MinimalAPI)
class UMaterialExpressionSpeedTree : public UMaterialExpression {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ESpeedTreeGeometryType> GeometryType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ESpeedTreeWindType> WindType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ESpeedTreeLODType> LODType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float BillboardThreshold;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bAccurateWindVelocities;
    
    UMaterialExpressionSpeedTree();

};

