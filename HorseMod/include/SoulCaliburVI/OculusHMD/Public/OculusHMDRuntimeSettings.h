#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "OculusSplashDesc.h"
#include "OculusHMDRuntimeSettings.generated.h"

UCLASS(Blueprintable, DefaultConfig, Config=Engine)
class OCULUSHMD_API UOculusHMDRuntimeSettings : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bAutoEnabled;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FOculusSplashDesc> SplashDescs;
    
    UOculusHMDRuntimeSettings();

};

