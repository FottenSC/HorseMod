#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxCreationSupplement.generated.h"

UCLASS(Abstract, Blueprintable)
class LUXORGAME_API ULuxCreationSupplement : public UObject {
    GENERATED_BODY()
public:
    ULuxCreationSupplement();

};

