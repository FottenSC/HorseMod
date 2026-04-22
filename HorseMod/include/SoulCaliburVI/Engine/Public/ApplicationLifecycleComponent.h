#pragma once
#include "CoreMinimal.h"
#include "ActorComponent.h"
#include "ApplicationLifecycleComponent.generated.h"

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API UApplicationLifecycleComponent : public UActorComponent {
    GENERATED_BODY()
public:
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FApplicationLifetimeDelegate);
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FApplicationLifetimeDelegate ApplicationWillDeactivateDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FApplicationLifetimeDelegate ApplicationHasReactivatedDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FApplicationLifetimeDelegate ApplicationWillEnterBackgroundDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FApplicationLifetimeDelegate ApplicationHasEnteredForegroundDelegate;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FApplicationLifetimeDelegate ApplicationWillTerminateDelegate;
    
    UApplicationLifecycleComponent(const FObjectInitializer& ObjectInitializer);

};

