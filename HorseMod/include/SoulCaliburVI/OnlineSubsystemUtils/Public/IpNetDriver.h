#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=NetDriver -FallbackName=NetDriver
#include "IpNetDriver.generated.h"

UCLASS(Blueprintable, NonTransient, Config=Engine)
class ONLINESUBSYSTEMUTILS_API UIpNetDriver : public UNetDriver {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 LogPortUnreach: 1;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 AllowPlayerPortUnreach: 1;
    
    UPROPERTY(Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 MaxPortCountToTry;
    
    UIpNetDriver();

};

