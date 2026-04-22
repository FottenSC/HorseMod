#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=SceneComponent -FallbackName=SceneComponent
#include "LuxTraceAttachComponent.generated.h"

UCLASS(Blueprintable, EditInlineNew, ClassGroup=Custom, meta=(BlueprintSpawnableComponent))
class LUXORGAME_API ULuxTraceAttachComponent : public USceneComponent {
    GENERATED_BODY()
public:
    ULuxTraceAttachComponent(const FObjectInitializer& ObjectInitializer);

};

