#pragma once
#include "CoreMinimal.h"
#include "ActorComponent.h"
#include "NavigationInvokerComponent.generated.h"

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API UNavigationInvokerComponent : public UActorComponent {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float TileGenerationRadius;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float TileRemovalRadius;
    
public:
    UNavigationInvokerComponent(const FObjectInitializer& ObjectInitializer);

};

