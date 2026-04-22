#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=HUD -FallbackName=HUD
#include "LuxHUD.generated.h"

UCLASS(Blueprintable, NonTransient)
class LUXORGAME_API ALuxHUD : public AHUD {
    GENERATED_BODY()
public:
    ALuxHUD(const FObjectInitializer& ObjectInitializer);

};

