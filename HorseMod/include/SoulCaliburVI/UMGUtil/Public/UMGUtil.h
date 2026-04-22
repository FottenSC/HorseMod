#pragma once
#include "CoreMinimal.h"
#include "UIObject.h"
#include "UMGUtil.generated.h"

class UUIDataStorage;
class UUIEventHub;
class UUIGameFlowAutomation;
class UUIGameFlowManager;
class UUIInputHandler;
class UUIWidgetInputHandlingManager;

UCLASS(Blueprintable)
class UMGUTIL_API UUMGUtil : public UUIObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UUIDataStorage* UIDataStorage;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UUIGameFlowManager* UIGameFlowManager;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UUIGameFlowAutomation* UIGameFlowAutomation;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UUIEventHub* UIEventHub;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UUIInputHandler* UIInputHandler;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UUIWidgetInputHandlingManager* UIWidgetInputHandlingManager;
    
    UUMGUtil();

    UFUNCTION(BlueprintCallable)
    void SetDirectInputEnabled(bool Enabled);
    
};

