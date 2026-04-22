#pragma once
#include "CoreMinimal.h"
#include "ELuxBattleMoveCategory.h"
#include "ELuxBattleMovePlayRequest.h"
#include "ELuxBattleMovePlayState.h"
#include "LuxBattleCommonActor.h"
#include "LuxBattleMovePlayInfo.h"
#include "LuxBattleMovePlayParam.h"
#include "LuxBattleMoveCommandPlayer.generated.h"

class ULuxBattleMovePlayData;

UCLASS(Blueprintable)
class LUXORGAME_API ALuxBattleMoveCommandPlayer : public ALuxBattleCommonActor {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    TArray<ULuxBattleMovePlayData*> PlayData;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxBattleMovePlayRequest Request;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLuxBattleMovePlayInfo RequestInfo;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxBattleMovePlayState PlayState;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLuxBattleMovePlayInfo PlayStateInfo;
    
public:
    ALuxBattleMoveCommandPlayer(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void StopMove();
    
    UFUNCTION(BlueprintCallable)
    void PlayMoveDirect(int32 inPlayerIndex, const FLuxBattleMovePlayParam& InParam);
    
    UFUNCTION(BlueprintCallable)
    void PlayMove(int32 inPlayerIndex, ELuxBattleMoveCategory inCategory, int32 inCategoryItemIndex);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool IsPlaying(int32 inPlayerIndex) const;
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool GetMovePlayParam(int32 inPlayerIndex, ELuxBattleMoveCategory inCategory, int32 inCategoryItemIndex, FLuxBattleMovePlayParam& outParam) const;
    
};

