#pragma once
#include "CoreMinimal.h"
#include "ERichCurveExtrapolation.h"
#include "IndexedCurve.h"
#include "RichCurveKey.h"
#include "RichCurve.generated.h"

USTRUCT(BlueprintType)
struct ENGINE_API FRichCurve : public FIndexedCurve {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ERichCurveExtrapolation> PreInfinityExtrap;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ERichCurveExtrapolation> PostInfinityExtrap;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float DefaultValue;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, EditFixedSize, meta=(AllowPrivateAccess=true))
    TArray<FRichCurveKey> Keys;
    
    FRichCurve();
};

