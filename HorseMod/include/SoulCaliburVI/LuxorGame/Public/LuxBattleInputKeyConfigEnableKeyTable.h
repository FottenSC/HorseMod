#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "LuxBattleInputKeyConfigEnableKeyTable.generated.h"

class UDataTable;

UCLASS(Blueprintable)
class ULuxBattleInputKeyConfigEnableKeyTable : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UDataTable* Table;
    
    ULuxBattleInputKeyConfigEnableKeyTable();

};

