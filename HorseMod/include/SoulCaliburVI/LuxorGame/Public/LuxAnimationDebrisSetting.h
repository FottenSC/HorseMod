#pragma once
#include "CoreMinimal.h"
#include "LuxAnimationDebrisSetting.generated.h"

class UAnimSequence;
class UMaterialInterface;
class USkeletalMesh;

USTRUCT(BlueprintType)
struct LUXORGAME_API FLuxAnimationDebrisSetting {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    USkeletalMesh* Mesh;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UAnimSequence* Animation;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 HeightOffset;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<UMaterialInterface*> BaseMaterialList;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<UMaterialInterface*> TranslucentMaterialList;
    
    FLuxAnimationDebrisSetting();
};

