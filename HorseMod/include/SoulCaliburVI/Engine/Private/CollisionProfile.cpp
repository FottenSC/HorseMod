#include "CollisionProfile.h"

UCollisionProfile::UCollisionProfile() {
    this->Profiles.AddDefaulted(19);
    this->ProfileRedirects.AddDefaulted(5);
    this->CollisionChannelRedirects.AddDefaulted(4);
}


