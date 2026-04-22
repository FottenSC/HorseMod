#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "UINodeStyle.generated.h"

UCLASS(Abstract, Blueprintable)
class UMGUTIL_API UUINodeStyle : public UDataAsset {
    GENERATED_BODY()
public:
    UUINodeStyle();

};

