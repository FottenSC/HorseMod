#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=NetConnection -FallbackName=NetConnection
#include "WebSocketConnection.generated.h"

UCLASS(Blueprintable, NonTransient)
class HTML5NETWORKING_API UWebSocketConnection : public UNetConnection {
    GENERATED_BODY()
public:
    UWebSocketConnection();

};

