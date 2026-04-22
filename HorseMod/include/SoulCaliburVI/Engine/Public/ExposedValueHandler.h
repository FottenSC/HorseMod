#pragma once
#include "CoreMinimal.h"
#include "ExposedValueCopyRecord.h"
#include "ExposedValueHandler.generated.h"

USTRUCT(BlueprintType)
struct ENGINE_API FExposedValueHandler {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName BoundFunction;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FExposedValueCopyRecord> CopyRecords;
    
    FExposedValueHandler();
};

