#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=CanvasRenderTarget2D -FallbackName=CanvasRenderTarget2D
#include "DMCanvasRT2D.generated.h"

UCLASS(Blueprintable)
class DMUTILITY_API UDMCanvasRT2D : public UCanvasRenderTarget2D {
    GENERATED_BODY()
public:
    UDMCanvasRT2D();

};

