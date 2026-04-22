#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=AnimInstance -FallbackName=AnimInstance
#include "LuxAnimNode_ModifyBoneArray.h"
#include "LuxTraceAnimInstance.generated.h"

UCLASS(Blueprintable, NonTransient)
class LUXORGAME_API ULuxTraceAnimInstance : public UAnimInstance {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FLuxAnimNode_ModifyBoneArray BoneArrayNode;
    
public:
    ULuxTraceAnimInstance();

};

