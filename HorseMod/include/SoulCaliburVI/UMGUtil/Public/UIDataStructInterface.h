#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=TableRowBase -FallbackName=TableRowBase
#include "UIDataStructInterface.generated.h"

USTRUCT(BlueprintType)
struct UMGUTIL_API FUIDataStructInterface : public FTableRowBase {
    GENERATED_BODY()
public:
    FUIDataStructInterface();
};

