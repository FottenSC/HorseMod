#pragma once
#include "CoreMinimal.h"
#include "ETriangleSortAxis.h"
#include "ETriangleSortOption.h"
#include "TriangleSortSettings.generated.h"

USTRUCT(BlueprintType)
struct FTriangleSortSettings {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ETriangleSortOption> TriangleSorting;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    TEnumAsByte<ETriangleSortAxis> CustomLeftRightAxis;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FName CustomLeftRightBoneName;
    
    ENGINE_API FTriangleSortSettings();
};

