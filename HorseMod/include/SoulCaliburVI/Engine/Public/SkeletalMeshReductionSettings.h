#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "SkeletalMeshLODGroupSettings.h"
#include "SkeletalMeshReductionSettings.generated.h"

UCLASS(Blueprintable, DefaultConfig, MinimalAPI, Config=Engine)
class USkeletalMeshReductionSettings : public UObject {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, GlobalConfig, meta=(AllowPrivateAccess=true))
    TArray<FSkeletalMeshLODGroupSettings> Settings;
    
public:
    USkeletalMeshReductionSettings();

};

