#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=OnlineSubsystem -ObjectName=ELanguage -FallbackName=ELanguage
#include "WFLData.generated.h"

USTRUCT(BlueprintType)
struct ONLINESUBSYSTEMUTILS_API FWFLData {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELanguage DataType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FString> WFLData;
    
    FWFLData();
};

