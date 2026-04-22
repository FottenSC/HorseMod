#pragma once
#include "CoreMinimal.h"
#include "StructSerializerObjectTestStruct.generated.h"

class UObject;

USTRUCT(BlueprintType)
struct FStructSerializerObjectTestStruct {
    GENERATED_BODY()
public:
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UClass* Class;
    
    UPROPERTY(BlueprintReadWrite, EditAnywhere, meta=(AllowPrivateAccess=true))
    UObject* ObjectPtr;
    
    SERIALIZATION_API FStructSerializerObjectTestStruct();
};

