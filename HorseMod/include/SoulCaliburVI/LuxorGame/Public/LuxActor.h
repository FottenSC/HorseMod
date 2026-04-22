#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Actor -FallbackName=Actor
#include "LuxActor.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxActor : public AActor {
    GENERATED_BODY()
public:
    ALuxActor(const FObjectInitializer& ObjectInitializer);

};

