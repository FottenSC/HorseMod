#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=WorldSettings -FallbackName=WorldSettings
#include "LuxWorldSettings.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxWorldSettings : public AWorldSettings {
    GENERATED_BODY()
public:
    ALuxWorldSettings(const FObjectInitializer& ObjectInitializer);

};

