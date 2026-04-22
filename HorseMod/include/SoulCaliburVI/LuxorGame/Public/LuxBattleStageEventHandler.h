#pragma once
#include "CoreMinimal.h"
#include "LuxActor.h"
#include "LuxFloorBreakParam.h"
#include "LuxWallBreakParam.h"
#include "LuxBattleStageEventHandler.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxBattleStageEventHandler : public ALuxActor {
    GENERATED_BODY()
public:
    ALuxBattleStageEventHandler(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveWallBreak(const FLuxWallBreakParam& inEvent);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveFloorBreak(const FLuxFloorBreakParam& inEvent);
    
};

