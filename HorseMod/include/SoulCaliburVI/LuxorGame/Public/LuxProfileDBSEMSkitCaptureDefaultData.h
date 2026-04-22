#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIDataStructInterface -FallbackName=UIDataStructInterface
#include "ELuxFacePattern.h"
#include "LuxSkitCaptureData.h"
#include "LuxProfileDBSEMSkitCaptureDefaultData.generated.h"

USTRUCT(BlueprintType)
struct FLuxProfileDBSEMSkitCaptureDefaultData : public FUIDataStructInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxFacePattern faceType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLuxSkitCaptureData skitData;
    
    LUXORGAME_API FLuxProfileDBSEMSkitCaptureDefaultData();
};

