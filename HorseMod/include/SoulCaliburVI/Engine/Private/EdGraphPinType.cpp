#include "EdGraphPinType.h"

FEdGraphPinType::FEdGraphPinType() {
    this->ContainerType = EPinContainerType::None;
    this->bIsMap = false;
    this->bIsSet = false;
    this->bIsArray = false;
    this->bIsReference = false;
    this->bIsConst = false;
    this->bIsWeakPointer = false;
}

