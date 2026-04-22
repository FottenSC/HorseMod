#pragma once
#include "CoreMinimal.h"
#include "LevelSequenceBindingReference.generated.h"

USTRUCT(BlueprintType)
struct FLevelSequenceBindingReference {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FString PackageName;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FString ObjectPath;
    
public:
    LEVELSEQUENCE_API FLevelSequenceBindingReference();
};

