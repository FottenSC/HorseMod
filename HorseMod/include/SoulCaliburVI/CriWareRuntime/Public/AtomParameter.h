#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Actor -FallbackName=Actor
#include "AtomParameter.generated.h"

UCLASS(Blueprintable, MinimalAPI)
class AAtomParameter : public AActor {
    GENERATED_BODY()
public:
    AAtomParameter(const FObjectInitializer& ObjectInitializer);

};

