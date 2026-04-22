#pragma once
#include "CoreMinimal.h"
#include "LuxProfileColorSlotListData.h"
#include "LuxProfileDataSerializable.h"
#include "LuxProfileSingleColorSlotListData.h"
#include "LuxProfileWeaponColorData.generated.h"

USTRUCT(BlueprintType)
struct FLuxProfileWeaponColorData : public FLuxProfileDataSerializable {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLuxProfileColorSlotListData weaponColor;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLuxProfileSingleColorSlotListData TraceColor;
    
    LUXORGAME_API FLuxProfileWeaponColorData();
};

