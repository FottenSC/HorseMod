#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "ELuxGroundMaterialType.h"
#include "LuxSEParamBase.h"
#include "LuxPlaySEParam.generated.h"

USTRUCT(BlueprintType)
struct LUXORGAME_API FLuxPlaySEParam : public FLuxSEParamBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FVector Position;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxGroundMaterialType Material;
    
    FLuxPlaySEParam();
};

