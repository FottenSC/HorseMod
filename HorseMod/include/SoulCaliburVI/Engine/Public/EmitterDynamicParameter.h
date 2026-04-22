#pragma once
#include "CoreMinimal.h"
#include "EEmitterDynamicParameterValue.h"
#include "RawDistributionFloat.h"
#include "EmitterDynamicParameter.generated.h"

USTRUCT(BlueprintType)
struct FEmitterDynamicParameter {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName ParamName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bUseEmitterTime: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bSpawnTimeOnly: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<EEmitterDynamicParameterValue> ValueMethod;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bScaleVelocityByParamValue: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRawDistributionFloat ParamValue;
    
    ENGINE_API FEmitterDynamicParameter();
};

