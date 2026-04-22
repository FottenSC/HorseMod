#pragma once
#include "CoreMinimal.h"
#include "StructSerializerBooleanTestStruct.generated.h"

USTRUCT(BlueprintType)
struct FStructSerializerBooleanTestStruct {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool BoolFalse;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    bool BoolTrue;
    
    UPROPERTY(EditAnywhere, meta=(AllowPrivateAccess=true))
    uint32 Bitfield;
    
    SERIALIZATION_API FStructSerializerBooleanTestStruct();
};

