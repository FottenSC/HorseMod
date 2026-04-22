#pragma once
#include "CoreMinimal.h"
#include "LuxBattleHUDBase.h"
#include "Templates/SubclassOf.h"
#include "LuxBattleCockpit.generated.h"

class UUserWidget;

UCLASS(Blueprintable)
class ALuxBattleCockpit : public ALuxBattleHUDBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TSubclassOf<UUserWidget> CockpitClass;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UUserWidget* CockpitInstance;
    
    ALuxBattleCockpit(const FObjectInitializer& ObjectInitializer);

};

