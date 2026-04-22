#include "CriFsLoader.h"
#include "CriFsLoaderComponent.h"

ACriFsLoader::ACriFsLoader(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {
    this->bHidden = true;
    this->bCanBeDamaged = false;
    this->RootComponent = CreateDefaultSubobject<UCriFsLoaderComponent>(TEXT("CriFsLoaderComponent0"));
    this->LoaderComponent = (UCriFsLoaderComponent*)RootComponent;
}


