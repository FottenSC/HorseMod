#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIDataStructInterface -FallbackName=UIDataStructInterface
#include "ELuxFacePattern.h"
#include "ELuxGender.h"
#include "ELuxRace.h"
#include "LuxSkitCaptureData.h"
#include "LuxProfileDBSEMSkitCaptureData.generated.h"

USTRUCT(BlueprintType)
struct FLuxProfileDBSEMSkitCaptureData : public FUIDataStructInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxRace racialType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxGender genderType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxFacePattern faceType;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLuxSkitCaptureData skitData;
    
    LUXORGAME_API FLuxProfileDBSEMSkitCaptureData();
};

