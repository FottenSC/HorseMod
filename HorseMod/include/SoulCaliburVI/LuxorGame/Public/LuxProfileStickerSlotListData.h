#pragma once
#include "CoreMinimal.h"
#include "LuxProfileDataSerializable.h"
#include "LuxProfileSingleStickerData.h"
#include "LuxProfileStickerSlotListData.generated.h"

USTRUCT(BlueprintType)
struct FLuxProfileStickerSlotListData : public FLuxProfileDataSerializable {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FLuxProfileSingleStickerData> slots;
    
    LUXORGAME_API FLuxProfileStickerSlotListData();
};

