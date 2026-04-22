#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=TableRowBase -FallbackName=TableRowBase
#include "VFxIDConvertTableInfo.generated.h"

USTRUCT(BlueprintType)
struct FVFxIDConvertTableInfo : public FTableRowBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 VFxID;
    
    LUXORGAME_API FVFxIDConvertTableInfo();
};

