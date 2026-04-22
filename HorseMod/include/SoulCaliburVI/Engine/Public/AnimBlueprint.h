#pragma once
#include "CoreMinimal.h"
#include "AnimGroupInfo.h"
#include "Blueprint.h"
#include "AnimBlueprint.generated.h"

class USkeleton;

UCLASS(Blueprintable)
class ENGINE_API UAnimBlueprint : public UBlueprint {
    GENERATED_BODY()
public:
    UPROPERTY(AssetRegistrySearchable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    USkeleton* TargetSkeleton;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FAnimGroupInfo> Groups;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bUseMultiThreadedAnimationUpdate;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bWarnAboutBlueprintUsage;
    
    UAnimBlueprint();

};

