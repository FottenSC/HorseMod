#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "LuxBattleInputPairs.h"
#include "LuxBattleInputBinding.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxBattleInputBinding : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLuxBattleInputPairs Bindings;
    
    ULuxBattleInputBinding();

};

