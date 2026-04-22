#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxGameSingleton.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxGameSingleton : public UObject {
    GENERATED_BODY()
public:
    ULuxGameSingleton();

};

