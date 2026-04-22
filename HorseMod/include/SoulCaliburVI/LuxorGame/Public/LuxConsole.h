#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=Console -FallbackName=Console
#include "LuxConsole.generated.h"

UCLASS(Blueprintable, NonTransient)
class LUXORGAME_API ULuxConsole : public UConsole {
    GENERATED_BODY()
public:
    ULuxConsole();

};

