#pragma once
#include "CoreMinimal.h"
#include "VFxSettingListDataValue.generated.h"

class ULuxGroundDebrisSettingListDataAsset;
class ULuxParticleSystemSettingListDataAsset;

USTRUCT(BlueprintType)
struct LUXORGAME_API FVFxSettingListDataValue {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ULuxParticleSystemSettingListDataAsset* PSSettingList;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ULuxGroundDebrisSettingListDataAsset* GDSettingList;
    
    FVFxSettingListDataValue();
};

