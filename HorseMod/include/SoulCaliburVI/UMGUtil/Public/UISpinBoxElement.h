#pragma once
#include "CoreMinimal.h"
#include "EUISpinBoxControlType.h"
#include "UIUserElement.h"
#include "UISpinBoxElement.generated.h"

UCLASS(Abstract, Blueprintable, EditInlineNew)
class UMGUTIL_API UUISpinBoxElement : public UUIUserElement {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    EUISpinBoxControlType spinboxControlType;
    
    UUISpinBoxElement();

};

