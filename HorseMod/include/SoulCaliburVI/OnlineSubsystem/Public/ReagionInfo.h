#pragma once
#include "CoreMinimal.h"
#include "ELargeRegion.h"
#include "ERegion.h"
#include "ReagionInfo.generated.h"

USTRUCT(BlueprintType)
struct ONLINESUBSYSTEM_API FReagionInfo {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ERegion Reasion;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELargeRegion LargeReasion;
    
    FReagionInfo();
};

