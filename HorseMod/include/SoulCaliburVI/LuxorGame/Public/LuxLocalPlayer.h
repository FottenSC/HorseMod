#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Vector2D -FallbackName=Vector2D
//CROSS-MODULE INCLUDE V2: -ModuleName=Engine -ObjectName=LocalPlayer -FallbackName=LocalPlayer
#include "LuxLocalPlayer.generated.h"

UCLASS(Blueprintable, NonTransient)
class LUXORGAME_API ULuxLocalPlayer : public ULocalPlayer {
    GENERATED_BODY()
public:
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool bOverrideScaling;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FVector2D OriginOverride;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FVector2D SizeOverride;
    
public:
    ULuxLocalPlayer();

    UFUNCTION(BlueprintCallable, BlueprintPure)
    bool ShouldOverrideViewportScaling() const;
    
    UFUNCTION(BlueprintCallable)
    void SetViewportScaling(const FVector2D& inOrigin, const FVector2D& InSize);
    
    UFUNCTION(BlueprintCallable, BlueprintPure)
    void GetViewportScaling(FVector2D& inOrigin, FVector2D& InSize) const;
    
    UFUNCTION(BlueprintCallable)
    void CancelViewportScaling();
    
};

