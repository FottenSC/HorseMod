#pragma once
#include "CoreMinimal.h"
#include "ELuxFightStyle.h"
#include "LuxStageSkeletalMeshActor.h"
#include "LuxKurokoActor.generated.h"

UCLASS(Blueprintable)
class ALuxKurokoActor : public ALuxStageSkeletalMeshActor {
    GENERATED_BODY()
public:
    ALuxKurokoActor(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void OnResultWinStarted(ELuxFightStyle STYLE, int32 PlayerId, int32 enshutuId, bool is_debug);
    
};

