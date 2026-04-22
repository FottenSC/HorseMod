#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=PlayerController -FallbackName=PlayerController
#include "LuxPlayerController.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxPlayerController : public APlayerController {
    GENERATED_BODY()
public:
    ALuxPlayerController(const FObjectInitializer& ObjectInitializer);

};

