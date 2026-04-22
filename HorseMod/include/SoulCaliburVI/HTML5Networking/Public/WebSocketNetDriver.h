#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=NetDriver -FallbackName=NetDriver
#include "WebSocketNetDriver.generated.h"

UCLASS(Blueprintable, NonTransient, Config=Engine)
class HTML5NETWORKING_API UWebSocketNetDriver : public UNetDriver {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 WebSocketPort;
    
    UWebSocketNetDriver();

};

