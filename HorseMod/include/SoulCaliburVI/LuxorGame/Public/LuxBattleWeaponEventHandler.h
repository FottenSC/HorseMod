#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector -FallbackName=Vector
#include "ELuxWeaponAttackType.h"
#include "LuxActor.h"
#include "LuxWeaponAttackTypeParam.h"
#include "LuxBattleWeaponEventHandler.generated.h"

class UDataTable;

UCLASS(Blueprintable)
class LUXORGAME_API ALuxBattleWeaponEventHandler : public ALuxActor {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    UDataTable* LuxBCWATTable;
    
public:
    ALuxBattleWeaponEventHandler(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveGetWeaponTip(const FLuxWeaponAttackTypeParam& inEvent, FVector& outRoot, FVector& outTip, bool& bReturnValue, bool& bGetType);
    
    UFUNCTION(BlueprintCallable, BlueprintImplementableEvent)
    void ReceiveGetWeaponAttackType(const FLuxWeaponAttackTypeParam& inEvent, ELuxWeaponAttackType& inAttackType, bool& bGetType);
    
};

