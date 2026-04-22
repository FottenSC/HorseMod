#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=TableRowBase -FallbackName=TableRowBase
#include "WeaponType.h"
#include "LuxBattleCharaWeaponAttackTypeTable.generated.h"

USTRUCT(BlueprintType)
struct FLuxBattleCharaWeaponAttackTypeTable : public FTableRowBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FWeaponType> WeaponTypeList;
    
    LUXORGAME_API FLuxBattleCharaWeaponAttackTypeTable();
};

