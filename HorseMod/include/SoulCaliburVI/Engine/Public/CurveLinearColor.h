#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=LinearColor -FallbackName=LinearColor
#include "CurveBase.h"
#include "RichCurve.h"
#include "CurveLinearColor.generated.h"

UCLASS(Blueprintable)
class ENGINE_API UCurveLinearColor : public UCurveBase {
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    FRichCurve FloatCurves[4];
    
    UCurveLinearColor();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    FLinearColor GetLinearColorValue(float InTime) const;
    
};

