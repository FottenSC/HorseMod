#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=Slate -ObjectName=ESelectionMode -FallbackName=ESelectionMode
#include "TableViewBase.h"
#include "TableViewBase.h"
#include "TileView.generated.h"

class UObject;

UCLASS(Blueprintable)
class UMG_API UTileView : public UTableViewBase {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float ItemWidth;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    float ItemHeight;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<UObject*> Items;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ESelectionMode::Type> SelectionMode;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UTableViewBase::FOnGenerateRowUObject OnGenerateTileEvent;
    
    UTileView();

    UFUNCTION(BlueprintCallable)
    void SetItemWidth(float Width);
    
    UFUNCTION(BlueprintCallable)
    void SetItemHeight(float Height);
    
    UFUNCTION(BlueprintCallable)
    void RequestListRefresh();
    
};

