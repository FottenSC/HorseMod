#pragma once
#include "CoreMinimal.h"
#include "ELuxBattleTrainingMode.h"
#include "LuxBattleCommonActor.h"
#include "LuxBattleTrainingManager.generated.h"

class ALuxBattleAICustomizer;
class ALuxBattleDummyCustomizer;
class ALuxBattleGaugeTypeChanger;
class ALuxBattleKeyRecorder;
class ALuxBattleMoveCommandPlayer;
class ALuxBattlePositionResetter;
class ALuxBattleTrainingReplayPlayer;

UCLASS(Blueprintable)
class LUXORGAME_API ALuxBattleTrainingManager : public ALuxBattleCommonActor {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxBattleTrainingMode mode;
    
public:
    ALuxBattleTrainingManager(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void StoreTrainingUIMenu();
    
    UFUNCTION(BlueprintCallable)
    void SetMode(ELuxBattleTrainingMode inMode);
    
    UFUNCTION(BlueprintCallable)
    void LoadTrainingUIMenu();
    
    UFUNCTION(BlueprintCallable)
    ALuxBattleTrainingReplayPlayer* GetTrainingReplayPlayer();
    
    UFUNCTION(BlueprintCallable)
    ALuxBattlePositionResetter* GetPositionResetter();
    
    UFUNCTION(BlueprintCallable)
    ALuxBattleMoveCommandPlayer* GetMoveCommandPlayer();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    ELuxBattleTrainingMode GetMode() const;
    
    UFUNCTION(BlueprintCallable)
    ALuxBattleKeyRecorder* GetKeyRecorder();
    
    UFUNCTION(BlueprintCallable)
    ALuxBattleGaugeTypeChanger* GetGaugeTypeChanger();
    
    UFUNCTION(BlueprintCallable)
    ALuxBattleDummyCustomizer* GetDummyCustomizer();
    
    UFUNCTION(BlueprintCallable)
    ALuxBattleAICustomizer* GetAICustomizer();
    
};

