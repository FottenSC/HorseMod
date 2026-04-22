#pragma once
#include "CoreMinimal.h"
#include "ELuxBattleStepMode.h"
#include "LuxBattleCommonActor.h"
#include "LuxBattlePauseController.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxBattlePauseController : public ALuxBattleCommonActor {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxBattleStepMode StepMode;
    
    ALuxBattlePauseController(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void OnTickWhenPaused();
    
};

