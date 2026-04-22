#pragma once
#include "CoreMinimal.h"
//CROSS-MODULE INCLUDE V2: -ModuleName=CoreUObject -ObjectName=StringAssetReference -FallbackName=StringAssetReference
#include "EditorMapPerformanceTestDefinition.generated.h"

USTRUCT(BlueprintType)
struct FEditorMapPerformanceTestDefinition {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStringAssetReference PerformanceTestmap;
    
    UPROPERTY(BlueprintReadWrite, Config, EditAnywhere, meta=(AllowPrivateAccess=true))
    int32 TestTimer;
    
    ENGINE_API FEditorMapPerformanceTestDefinition();
};

