#pragma once
#include "CoreMinimal.h"
#include "ContentWidget.h"
#include "SizeBox.generated.h"

UCLASS(Blueprintable)
class UMG_API USizeBox : public UContentWidget {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOverride_WidthOverride: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOverride_HeightOverride: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOverride_MinDesiredWidth: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOverride_MinDesiredHeight: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOverride_MaxDesiredWidth: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOverride_MaxDesiredHeight: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bOverride_MaxAspectRatio: 1;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float WidthOverride;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float HeightOverride;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MinDesiredWidth;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MinDesiredHeight;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MaxDesiredWidth;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MaxDesiredHeight;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float MaxAspectRatio;
    
    USizeBox();

    UFUNCTION(BlueprintCallable)
    void SetWidthOverride(float InWidthOverride);
    
    UFUNCTION(BlueprintCallable)
    void SetMinDesiredWidth(float InMinDesiredWidth);
    
    UFUNCTION(BlueprintCallable)
    void SetMinDesiredHeight(float InMinDesiredHeight);
    
    UFUNCTION(BlueprintCallable)
    void SetMaxDesiredWidth(float InMaxDesiredWidth);
    
    UFUNCTION(BlueprintCallable)
    void SetMaxDesiredHeight(float InMaxDesiredHeight);
    
    UFUNCTION(BlueprintCallable)
    void SetMaxAspectRatio(float InMaxAspectRatio);
    
    UFUNCTION(BlueprintCallable)
    void SetHeightOverride(float InHeightOverride);
    
    UFUNCTION(BlueprintCallable)
    void ClearWidthOverride();
    
    UFUNCTION(BlueprintCallable)
    void ClearMinDesiredWidth();
    
    UFUNCTION(BlueprintCallable)
    void ClearMinDesiredHeight();
    
    UFUNCTION(BlueprintCallable)
    void ClearMaxDesiredWidth();
    
    UFUNCTION(BlueprintCallable)
    void ClearMaxDesiredHeight();
    
    UFUNCTION(BlueprintCallable)
    void ClearMaxAspectRatio();
    
    UFUNCTION(BlueprintCallable)
    void ClearHeightOverride();
    
};

