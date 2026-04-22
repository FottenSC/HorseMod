#pragma once
#include "CoreMinimal.h"
#include "ESectionEvaluationFlags.h"
#include "SectionEvaluationData.generated.h"

USTRUCT(BlueprintType)
struct FSectionEvaluationData {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 ImplIndex;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float ForcedTime;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ESectionEvaluationFlags Flags;
    
    MOVIESCENE_API FSectionEvaluationData();
};

