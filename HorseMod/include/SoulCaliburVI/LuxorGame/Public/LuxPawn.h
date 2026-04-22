#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Pawn -FallbackName=Pawn
#include "LuxPawn.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxPawn : public APawn {
    GENERATED_BODY()
public:
    ALuxPawn(const FObjectInitializer& ObjectInitializer);

};

