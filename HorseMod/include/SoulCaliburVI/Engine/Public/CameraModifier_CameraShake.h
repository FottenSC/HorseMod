#pragma once
#include "CoreMinimal.h"
#include "CameraModifier.h"
#include "CameraModifier_CameraShake.generated.h"

class UCameraShake;

UCLASS(Blueprintable, Config=Camera)
class ENGINE_API UCameraModifier_CameraShake : public UCameraModifier {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<UCameraShake*> ActiveShakes;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float SplitScreenShakeScale;
    
public:
    UCameraModifier_CameraShake();

};

