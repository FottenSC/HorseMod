#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Character -FallbackName=Character
#include "LuxStageWolfCharacter.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxStageWolfCharacter : public ACharacter {
    GENERATED_BODY()
public:
    ALuxStageWolfCharacter(const FObjectInitializer& ObjectInitializer);

};

