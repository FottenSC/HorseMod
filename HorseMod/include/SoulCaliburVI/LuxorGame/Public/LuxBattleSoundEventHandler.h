#pragma once
#include "CoreMinimal.h"
#include "LuxActor.h"
#include "LuxPlaySEParam.h"
#include "LuxPlayVoiceParam.h"
#include "LuxStopSEParam.h"
#include "LuxStopVoiceParam.h"
#include "LuxBattleSoundEventHandler.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxBattleSoundEventHandler : public ALuxActor {
    GENERATED_BODY()
public:
    ALuxBattleSoundEventHandler(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveStopVoice(const FLuxStopVoiceParam& inEvent);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveStopSE(const FLuxStopSEParam& inEvent);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceivePlayVoice(const FLuxPlayVoiceParam& inEvent);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceivePlaySE(const FLuxPlaySEParam& inEvent);
    
};

