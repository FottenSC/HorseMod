#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "UMGSequencePlayer.generated.h"

class UWidgetAnimation;

UCLASS(Blueprintable, Transient)
class UMG_API UUMGSequencePlayer : public UObject {
    GENERATED_BODY()
public:
private:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Instanced, meta=(AllowPrivateAccess=true))
    UWidgetAnimation* Animation;
    
public:
    UUMGSequencePlayer();

};

