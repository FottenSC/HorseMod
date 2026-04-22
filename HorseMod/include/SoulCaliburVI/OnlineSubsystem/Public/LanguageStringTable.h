#pragma once
#include "CoreMinimal.h"
#include "ELanguage.h"
#include "LanguageStringTable.generated.h"

USTRUCT(BlueprintType)
struct ONLINESUBSYSTEM_API FLanguageStringTable {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TMap<FString, ELanguage> DataTable;
    
    FLanguageStringTable();
};

