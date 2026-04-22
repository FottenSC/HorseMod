#pragma once
#include "CoreMinimal.h"
#include "ActiveHapticFeedbackEffect.generated.h"

class UHapticFeedbackEffect_Base;

USTRUCT(BlueprintType)
struct FActiveHapticFeedbackEffect {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UHapticFeedbackEffect_Base* HapticEffect;
    
    ENGINE_API FActiveHapticFeedbackEffect();
};

