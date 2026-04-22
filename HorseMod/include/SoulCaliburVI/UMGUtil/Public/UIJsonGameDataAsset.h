#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=DataAsset -FallbackName=DataAsset
#include "Templates/SubclassOf.h"
#include "UIJsonGameDataAsset.generated.h"

class UUIGameDataObject;

UCLASS(Blueprintable)
class UMGUTIL_API UUIJsonGameDataAsset : public UDataAsset {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<TSubclassOf<UUIGameDataObject>> GameDataClasses;
    
    UUIJsonGameDataAsset();

};

