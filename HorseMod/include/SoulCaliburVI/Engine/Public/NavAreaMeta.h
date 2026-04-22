#pragma once
#include "CoreMinimal.h"
#include "NavArea.h"
#include "NavAreaMeta.generated.h"

UCLASS(Abstract, Blueprintable)
class ENGINE_API UNavAreaMeta : public UNavArea {
    GENERATED_BODY()
public:
    UNavAreaMeta();

};

