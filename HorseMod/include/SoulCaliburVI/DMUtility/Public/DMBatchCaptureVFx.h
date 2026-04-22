#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Emitter -FallbackName=Emitter
#include "DMBatchCaptureVFx.generated.h"

UCLASS(Blueprintable)
class DMUTILITY_API ADMBatchCaptureVFx : public AEmitter {
    GENERATED_BODY()
public:
    ADMBatchCaptureVFx(const FObjectInitializer& ObjectInitializer);

};

