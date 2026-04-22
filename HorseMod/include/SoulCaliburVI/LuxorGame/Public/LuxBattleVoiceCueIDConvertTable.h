#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=TableRowBase -FallbackName=TableRowBase
#include "LuxBattleVoiceCueIDConvertTable.generated.h"

USTRUCT(BlueprintType)
struct FLuxBattleVoiceCueIDConvertTable : public FTableRowBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 VoiceCueID;
    
    LUXORGAME_API FLuxBattleVoiceCueIDConvertTable();
};

