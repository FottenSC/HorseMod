#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector2D -FallbackName=Vector2D
#include "ELuxAttackTouchLevel.h"
#include "LuxActor.h"
#include "LuxDamageInfo.h"
#include "LuxDamageInfoEventListener.generated.h"

UCLASS(Blueprintable)
class ALuxDamageInfoEventListener : public ALuxActor {
    GENERATED_BODY()
public:
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDamageTypeEvent, int32, PlayerSide, ELuxAttackTouchLevel, AttackType, FVector2D, ScreenPos);
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDamageEvent, FLuxDamageInfo, Info);
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnDamageEvent OnDamageEvent;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnDamageTypeEvent OnDamageTypeEvent;
    
    ALuxDamageInfoEventListener(const FObjectInitializer& ObjectInitializer);

};

