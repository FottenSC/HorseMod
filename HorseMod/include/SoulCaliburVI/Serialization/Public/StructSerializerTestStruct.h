#pragma once
#include "CoreMinimal.h"
#include "StructSerializerArrayTestStruct.h"
#include "StructSerializerBooleanTestStruct.h"
#include "StructSerializerBuiltinTestStruct.h"
#include "StructSerializerMapTestStruct.h"
#include "StructSerializerNumericTestStruct.h"
#include "StructSerializerObjectTestStruct.h"
#include "StructSerializerTestStruct.generated.h"

USTRUCT(BlueprintType)
struct FStructSerializerTestStruct {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStructSerializerNumericTestStruct Numerics;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStructSerializerBooleanTestStruct Booleans;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStructSerializerObjectTestStruct Objects;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStructSerializerBuiltinTestStruct Builtins;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStructSerializerArrayTestStruct Arrays;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    FStructSerializerMapTestStruct Maps;
    
    SERIALIZATION_API FStructSerializerTestStruct();
};

