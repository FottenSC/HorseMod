#pragma once
#include "CoreMinimal.h"
#include "PrimitiveComponent.h"
#include "NavTestRenderingComponent.generated.h"

UCLASS(Blueprintable, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class UNavTestRenderingComponent : public UPrimitiveComponent {
    GENERATED_BODY()
public:
    UNavTestRenderingComponent(const FObjectInitializer& ObjectInitializer);

};

