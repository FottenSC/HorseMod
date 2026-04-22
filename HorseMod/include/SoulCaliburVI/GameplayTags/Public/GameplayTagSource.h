#pragma once
#include "CoreMinimal.h"
#include "EGameplayTagSourceType.h"
#include "GameplayTagSource.generated.h"

class UGameplayTagsList;

USTRUCT(BlueprintType)
struct FGameplayTagSource {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName SourceName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    EGameplayTagSourceType SourceType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UGameplayTagsList* SourceTagList;
    
    GAMEPLAYTAGS_API FGameplayTagSource();
};

