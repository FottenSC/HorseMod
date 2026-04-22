#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIDataStructInterface -FallbackName=UIDataStructInterface
#include "ELuxCreationDecorMenuFactorDataKey.h"
#include "ELuxStickerGuide.h"
#include "LuxProfileDBStickerGuideOption.generated.h"

USTRUCT(BlueprintType)
struct FLuxProfileDBStickerGuideOption : public FUIDataStructInterface {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    ELuxCreationDecorMenuFactorDataKey Key;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<ELuxStickerGuide> guideList;
    
    LUXORGAME_API FLuxProfileDBStickerGuideOption();
};

