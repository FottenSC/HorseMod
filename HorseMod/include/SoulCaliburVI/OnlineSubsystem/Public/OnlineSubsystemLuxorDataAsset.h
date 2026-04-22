#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LanguageStringTable.h"
#include "ReagionInfo.h"
#include "OnlineSubsystemLuxorDataAsset.generated.h"

UCLASS(Blueprintable)
class ONLINESUBSYSTEM_API UOnlineSubsystemLuxorDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<FString, FReagionInfo> ReagionTable;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<FString, FLanguageStringTable> LanguageTable;
    
    UOnlineSubsystemLuxorDataAsset();

};

