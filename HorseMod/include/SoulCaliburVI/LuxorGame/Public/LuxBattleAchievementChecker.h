#pragma once
#include "CoreMinimal.h"
#include "ELuxBattleMode.h"
#include "LuxBattleAchievement.h"
#include "LuxBattleCommonActor.h"
#include "LuxBattleAchievementChecker.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ALuxBattleAchievementChecker : public ALuxBattleCommonActor {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, EditFixedSize, meta=(AllowPrivateAccess=true))
    TArray<FLuxBattleAchievement> Items;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxBattleMode BattleMode;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<int32> MessageFlags;
    
public:
    ALuxBattleAchievementChecker(const FObjectInitializer& ObjectInitializer);

};

