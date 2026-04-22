#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=GameViewportClient -FallbackName=GameViewportClient
#include "DMGameViewportClientEx.generated.h"

class UObject;

UCLASS(Blueprintable, NonTransient)
class UDMGameViewportClientEx : public UGameViewportClient {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<UObject*> DbgCdr;
    
public:
    UDMGameViewportClientEx();

};

