#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=PrimitiveComponent -FallbackName=PrimitiveComponent
#include "LuxGroundDebrisComponent.generated.h"

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class LUXORGAME_API ULuxGroundDebrisComponent : public UPrimitiveComponent {
    GENERATED_BODY()
public:
    ULuxGroundDebrisComponent(const FObjectInitializer& ObjectInitializer);

};

