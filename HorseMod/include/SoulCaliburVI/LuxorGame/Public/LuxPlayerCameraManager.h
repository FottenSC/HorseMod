#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=PlayerCameraManager -FallbackName=PlayerCameraManager
#include "LuxPlayerCameraManager.generated.h"

UCLASS(Blueprintable, NonTransient)
class LUXORGAME_API ALuxPlayerCameraManager : public APlayerCameraManager {
    GENERATED_BODY()
public:
    ALuxPlayerCameraManager(const FObjectInitializer& ObjectInitializer);

};

