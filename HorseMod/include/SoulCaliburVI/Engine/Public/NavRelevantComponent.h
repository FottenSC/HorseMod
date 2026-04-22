#pragma once
#include "CoreMinimal.h"
#include "ActorComponent.h"
#include "NavRelevantInterface.h"
#include "NavRelevantComponent.generated.h"

class UObject;

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API UNavRelevantComponent : public UActorComponent, public INavRelevantInterface {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAttachToOwnersRoot: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UObject* CachedNavParent;
    
public:
    UNavRelevantComponent(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void SetNavigationRelevancy(bool bRelevant);
    

    // Fix for true pure virtual functions not being implemented
};

