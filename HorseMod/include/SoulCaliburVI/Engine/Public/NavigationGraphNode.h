#pragma once
#include "CoreMinimal.h"
#include "Actor.h"
#include "NavigationGraphNode.generated.h"

UCLASS(Abstract, Blueprintable, MinimalAPI)
class ANavigationGraphNode : public AActor {
    GENERATED_BODY()
public:
    ANavigationGraphNode(const FObjectInitializer& ObjectInitializer);

};

