#pragma once
#include "CoreMinimal.h"
#include "Actor.h"
#include "ERuntimeGenerationType.h"
#include "NavDataConfig.h"
#include "SupportedAreaData.h"
#include "NavigationData.generated.h"

class UPrimitiveComponent;

UCLASS(Abstract, Blueprintable, DefaultConfig, Config=Engine)
class ENGINE_API ANavigationData : public AActor {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, DuplicateTransient, EditAnywhere, Instanced, Transient, meta=(AllowPrivateAccess=true))
    UPrimitiveComponent* RenderingComp;
    
protected:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FNavDataConfig NavDataConfig;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Transient, meta=(AllowPrivateAccess=true))
    uint32 bEnableDrawing: 1;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bForceRebuildOnLoad: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCanBeMainNavData: 1;
    
    UPROPERTY(AdvancedDisplay, BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bCanSpawnOnRebuild: 1;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 bRebuildAtRuntime: 1;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    ERuntimeGenerationType RuntimeGeneration;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    float ObservedPathsTickInterval;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 DataVersion;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TArray<FSupportedAreaData> SupportedAreas;
    
public:
    ANavigationData(const FObjectInitializer& ObjectInitializer);

};

