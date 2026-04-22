#pragma once
#include "CoreMinimal.h"
#include "BoxElement2D.h"
#include "CircleElement2D.h"
#include "ConvexElement2D.h"
#include "AggregateGeometry2D.generated.h"

USTRUCT(BlueprintType)
struct ENGINE_API FAggregateGeometry2D {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, EditFixedSize, meta=(AllowPrivateAccess=true))
    TArray<FCircleElement2D> CircleElements;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, EditFixedSize, meta=(AllowPrivateAccess=true))
    TArray<FBoxElement2D> BoxElements;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, EditFixedSize, meta=(AllowPrivateAccess=true))
    TArray<FConvexElement2D> ConvexElements;
    
    FAggregateGeometry2D();
};

