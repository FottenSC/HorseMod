#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=LevelScriptActor -FallbackName=LevelScriptActor
#include "LuxLevelScriptActor.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxLevelScriptActor : public ALevelScriptActor {
    GENERATED_BODY()
public:
    ALuxLevelScriptActor(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void SendMessageToOtherLevel(const FString& Message);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void OnReceiveMessage(const FString& Message);
    
};

