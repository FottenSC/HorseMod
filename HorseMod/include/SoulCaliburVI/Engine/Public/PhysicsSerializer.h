#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "PhysicsSerializer.generated.h"

UCLASS(Blueprintable, DefaultToInstanced, MinimalAPI)
class UPhysicsSerializer : public UObject {
    GENERATED_BODY()
public:
    UPhysicsSerializer();

};

