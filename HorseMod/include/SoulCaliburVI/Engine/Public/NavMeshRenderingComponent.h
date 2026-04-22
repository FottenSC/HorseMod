#pragma once
#include "CoreMinimal.h"
#include "PrimitiveComponent.h"
#include "NavMeshRenderingComponent.generated.h"

UCLASS(Blueprintable, EditInlineNew, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API UNavMeshRenderingComponent : public UPrimitiveComponent {
    GENERATED_BODY()
public:
    UNavMeshRenderingComponent(const FObjectInitializer& ObjectInitializer);

};

