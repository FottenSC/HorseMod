#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=TableRowBase -FallbackName=TableRowBase
#include "ELuxEnshutsuSubId.h"
#include "ELuxEnshutsuVoiceLabel.h"
#include "LuxEnshutsuSubtitleData.generated.h"

USTRUCT(BlueprintType)
struct LUXORGAME_API FLuxEnshutsuSubtitleData : public FTableRowBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxEnshutsuVoiceLabel EnshutsuVoiceLabel;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxEnshutsuSubId EnshutsuSubId;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FString LocText;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 JPDelayFrame;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 ENDelayFrame;
    
    FLuxEnshutsuSubtitleData();
};

