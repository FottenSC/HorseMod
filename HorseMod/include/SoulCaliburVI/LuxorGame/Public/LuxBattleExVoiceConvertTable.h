#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=TableRowBase -FallbackName=TableRowBase
#include "LuxBattleExVoiceConvertTable.generated.h"

USTRUCT(BlueprintType)
struct FLuxBattleExVoiceConvertTable : public FTableRowBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 ConvertedVoiceCueID;
    
    LUXORGAME_API FLuxBattleExVoiceConvertTable();
};

