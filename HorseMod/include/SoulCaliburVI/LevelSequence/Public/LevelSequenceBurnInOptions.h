#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=StringClassReference -FallbackName=StringClassReference
#include "LevelSequenceBurnInOptions.generated.h"

class ULevelSequenceBurnInInitSettings;

UCLASS(Blueprintable, DefaultToInstanced)
class LEVELSEQUENCE_API ULevelSequenceBurnInOptions : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bUseBurnIn;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStringClassReference BurnInClass;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    ULevelSequenceBurnInInitSettings* Settings;
    
    ULevelSequenceBurnInOptions();

};

