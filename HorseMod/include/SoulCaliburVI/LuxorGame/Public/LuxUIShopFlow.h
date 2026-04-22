#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIObject -FallbackName=UIObject
#include "LuxUIShopFlowParam.h"
#include "LuxUIShopFlow.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUIShopFlow : public UUIObject {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLuxUIShopFlowParam Param;
    
public:
    ULuxUIShopFlow();

};

