#pragma once
#include "CoreMinimal.h"
#include "Widget.h"
#include "TableViewBase.generated.h"

class UObject;

UCLASS(Abstract, Blueprintable)
class UMG_API UTableViewBase : public UWidget {
    GENERATED_BODY()
public:
    DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(UWidget*, FOnGenerateRowUObject, UObject*, Item);
    
    UTableViewBase();

};

