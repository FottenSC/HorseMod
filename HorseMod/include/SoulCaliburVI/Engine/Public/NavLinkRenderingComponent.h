#pragma once
#include "CoreMinimal.h"
#include "PrimitiveComponent.h"
#include "NavLinkRenderingComponent.generated.h"

UCLASS(Blueprintable, EditInlineNew, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class ENGINE_API UNavLinkRenderingComponent : public UPrimitiveComponent {
    GENERATED_BODY()
public:
    UNavLinkRenderingComponent(const FObjectInitializer& ObjectInitializer);

};

