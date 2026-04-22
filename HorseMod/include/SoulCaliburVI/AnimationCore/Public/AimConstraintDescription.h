#pragma once
#include "CoreMinimal.h"
#include "Axis.h"
#include "ConstraintDescriptionEx.h"
#include "AimConstraintDescription.generated.h"

USTRUCT(BlueprintType)
struct ANIMATIONCORE_API FAimConstraintDescription : public FConstraintDescriptionEx {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FAxis LookAt_Axis;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FAxis LookUp_Axis;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bUseLookUp;
    
    FAimConstraintDescription();
};

