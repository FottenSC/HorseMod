#pragma once
#include "CoreMinimal.h"
#include "AggregateGeometry2D.h"
#include "BodySetup.h"
#include "BodySetup2D.generated.h"

UCLASS(Blueprintable, CollapseCategories)
class ENGINE_API UBodySetup2D : public UBodySetup {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FAggregateGeometry2D AggGeom2D;
    
    UBodySetup2D();

};

