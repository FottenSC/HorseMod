#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=Object -FallbackName=Object
#include "OnEndSubtitleDelegate.h"
#include "OnStartSubtitleDelegate.h"
#include "LuxStorySubtitleHandler.generated.h"

class UDataTable;

UCLASS(Blueprintable)
class ULuxStorySubtitleHandler : public UObject {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnStartSubtitle StartSubtitleEvent;
    
    UPROPERTY(BlueprintAssignable, BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FOnEndSubtitle EndSubtitleEvent;
    
    ULuxStorySubtitleHandler();

    UFUNCTION(BlueprintCallable)
    void Update(float TotalTime);
    
    UFUNCTION(BlueprintCallable)
    void Setup(UDataTable* SubtitleInfoList);
    
};

