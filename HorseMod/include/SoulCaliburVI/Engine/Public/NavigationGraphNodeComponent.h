#pragma once
#include "CoreMinimal.h"
#include "NavGraphNode.h"
#include "SceneComponent.h"
#include "NavigationGraphNodeComponent.generated.h"

class UNavigationGraphNodeComponent;

UCLASS(Blueprintable, MinimalAPI, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class UNavigationGraphNodeComponent : public USceneComponent {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FNavGraphNode Node;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UNavigationGraphNodeComponent* NextNodeComponent;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UNavigationGraphNodeComponent* PrevNodeComponent;
    
    UNavigationGraphNodeComponent(const FObjectInitializer& ObjectInitializer);

};

