#pragma once
#include "CoreMinimal.h"
#include "LuxBattleHUDBase.h"
#include "Templates/SubclassOf.h"
#include "LuxBattleStage.generated.h"

class UUserWidget;

UCLASS(Blueprintable)
class ALuxBattleStage : public ALuxBattleHUDBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TSubclassOf<UUserWidget> BGMTitleClass;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UUserWidget* BGMTitleInstance;
    
    ALuxBattleStage(const FObjectInitializer& ObjectInitializer);

};

