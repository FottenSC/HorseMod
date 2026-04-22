#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=BlueprintFunctionLibrary -FallbackName=BlueprintFunctionLibrary
//CROSS-MODULE INCLUDE V2: -ModuleName=UMGUtil -ObjectName=UIDataObject -FallbackName=UIDataObject
#include "LuxChronicleUtil.generated.h"

UCLASS(Blueprintable)
class LUXORGAME_API ULuxChronicleUtil : public UBlueprintFunctionLibrary {
    GENERATED_BODY()
public:
    ULuxChronicleUtil();

    UFUNCTION(BlueprintCallable)
    static void SetNoticeNavi();
    
    UFUNCTION(BlueprintCallable)
    static void SetChronologyNavi();
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    static bool GetChronicleAllMissionData(FUIDataObject& MissionData);
    
};

