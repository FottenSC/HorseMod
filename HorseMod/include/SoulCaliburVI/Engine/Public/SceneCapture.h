#pragma once
#include "CoreMinimal.h"
#include "Actor.h"
#include "SceneCapture.generated.h"

class UStaticMeshComponent;

UCLASS(Abstract, Blueprintable, MinimalAPI)
class ASceneCapture : public AActor {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UStaticMeshComponent* MeshComp;
    
public:
    ASceneCapture(const FObjectInitializer& ObjectInitializer);

};

