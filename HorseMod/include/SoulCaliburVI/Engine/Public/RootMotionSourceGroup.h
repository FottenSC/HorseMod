#pragma once
#include "CoreMinimal.h"
#include "RootMotionSourceSettings.h"
#include "Vector_NetQuantize10.h"
#include "RootMotionSourceGroup.generated.h"

USTRUCT(BlueprintType)
struct ENGINE_API FRootMotionSourceGroup {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bHasAdditiveSources;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bHasOverrideSources;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FVector_NetQuantize10 LastPreAdditiveVelocity;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bIsAdditiveVelocityApplied;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FRootMotionSourceSettings LastAccumulatedSettings;
    
    FRootMotionSourceGroup();
};

