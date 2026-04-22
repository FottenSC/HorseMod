#pragma once
#include "CoreMinimal.h"
#include "Actor.h"
#include "ActorFractureSignatureDelegate.h"
#include "DestructibleActor.generated.h"

class UDestructibleComponent;

UCLASS(Blueprintable, Config=Engine)
class ENGINE_API ADestructibleActor : public AActor {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UDestructibleComponent* DestructibleComponent;
    
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bAffectNavigation: 1;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FActorFractureSignature OnActorFracture;
    
    ADestructibleActor(const FObjectInitializer& ObjectInitializer);

};

