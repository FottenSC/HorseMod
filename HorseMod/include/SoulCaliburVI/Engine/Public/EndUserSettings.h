#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "ImportantToggleSettingInterface.h"
#include "EndUserSettings.generated.h"

UCLASS(Blueprintable, DefaultConfig, MinimalAPI, Config=Engine)
class UEndUserSettings : public UObject, public IImportantToggleSettingInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bSendAnonymousUsageDataToEpic;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bSendMeanTimeBetweenFailureDataToEpic;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bAllowUserIdInUsageData;
    
    UEndUserSettings();


    // Fix for true pure virtual functions not being implemented
};

