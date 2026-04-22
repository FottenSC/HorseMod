#pragma once
#include "CoreMinimal.h"
#include "UIEventSignatureDelegate.h"
#include "UIObject.h"
#include "UIEventListener.generated.h"

UCLASS(Blueprintable)
class UMGUTIL_API UUIEventListener : public UUIObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FUIEventSignature UIEvent;
    
    UUIEventListener();

};

