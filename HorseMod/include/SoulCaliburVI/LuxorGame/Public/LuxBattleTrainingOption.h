#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIDataObject -FallbackName=UIDataObject
#include "LuxBattleHUDBase.h"
#include "Templates/SubclassOf.h"
#include "LuxBattleTrainingOption.generated.h"

class UUserWidget;

UCLASS(Blueprintable)
class ALuxBattleTrainingOption : public ALuxBattleHUDBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<TSubclassOf<UUserWidget>> TrainingOptionClass;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    TArray<UUserWidget*> TrainingOptionInstance;
    
    ALuxBattleTrainingOption(const FObjectInitializer& ObjectInitializer);

    UFUNCTION(BlueprintCallable)
    void OnDataLoadComplete(const FUIDataObject& Event);
    
};

