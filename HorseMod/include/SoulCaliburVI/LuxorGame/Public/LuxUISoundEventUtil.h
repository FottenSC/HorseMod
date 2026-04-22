#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIDataObject -FallbackName=UIDataObject
#include "ELuxMenuCategoryType.h"
#include "ELuxMenuSoundEventType.h"
#include "LuxUISoundEventUtil.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxUISoundEventUtil : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxUISoundEventUtil();

    UFUNCTION(BlueprintCallable)
    static bool SendMenuSoundEvent(ELuxMenuCategoryType CategoryType, ELuxMenuSoundEventType Trigger, FUIDataObject Param);
    
};

