#pragma once
#include "CoreMinimal.h"
#include "CompositeSubFont.h"
#include "Typeface.h"
#include "CompositeFont.generated.h"

USTRUCT(BlueprintType)
struct SLATECORE_API FCompositeFont {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FTypeface DefaultTypeface;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FCompositeSubFont> SubTypefaces;
    
    FCompositeFont();
};

