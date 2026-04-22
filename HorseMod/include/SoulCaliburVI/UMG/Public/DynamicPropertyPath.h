#pragma once
#include "CoreMinimal.h"
#include "PropertyPathSegment.h"
#include "DynamicPropertyPath.generated.h"

USTRUCT(BlueprintType)
struct UMG_API FDynamicPropertyPath {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FPropertyPathSegment> Segments;
    
public:
    FDynamicPropertyPath();
};

