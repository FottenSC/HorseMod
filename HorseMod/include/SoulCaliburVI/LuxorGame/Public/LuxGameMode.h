#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=GameMode -FallbackName=GameMode
#include "LuxGameMode.generated.h"

UCLASS(Blueprintable, NonTransient)
class LUXORGAME_API ALuxGameMode : public AGameMode {
    GENERATED_BODY()
public:
    ALuxGameMode(const FObjectInitializer& ObjectInitializer);

};

