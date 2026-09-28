#include <Windows.h>
#include <cstdint>
#include <cstring>
template<class T>T At(std::uintptr_t p){T v;std::memcpy(&v,reinterpret_cast<void*>(p),sizeof(v));return v;}
static const int callbacks=7;
struct Control {
    const void* vtable{&callbacks};std::uintptr_t base{},registry{};
#include "lease_membership.inl"
};
struct Status {bool value;bool ok() const{return value;}};
struct Source {Control* control;Status ValidateHeld(std::uintptr_t,void*)const{return {control->Membership()};}};
using Validator=bool(*)(std::uintptr_t,void*,const Source*);
#ifdef OWNER_MODULE
extern "C" __declspec(dllexport) Source* Create() {
    auto* c=new Control;
    c->base=reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,0x4300000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    c->registry=c->base+0x1000;
    *reinterpret_cast<std::uintptr_t*>(c->base+0x429eac8)=c->registry;
    *reinterpret_cast<std::uintptr_t*>(c->registry)=c->base+0x350f7e0;
    auto** entries=reinterpret_cast<void**>(c->base+0x2000);entries[0]=c;
    *reinterpret_cast<void***>(c->registry+0x28)=entries;
    *reinterpret_cast<int*>(c->registry+0x30)=1;
    *reinterpret_cast<int*>(c->registry+0x34)=1;
    InitializeCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(c->registry+0x38));
    return new Source{c};
}
extern "C" __declspec(dllexport) bool Validate(std::uintptr_t base,void* battle,const Source* s){return s->ValidateHeld(base,battle).ok();}
extern "C" __declspec(dllexport) void Release(Source* s){auto* c=s->control;DeleteCriticalSection(reinterpret_cast<LPCRITICAL_SECTION>(c->registry+0x38));VirtualFree(reinterpret_cast<void*>(c->base),0,MEM_RELEASE);delete c;delete s;}
#else
static bool Prefix(std::uintptr_t base,void* battle,const Source& source_image,Validator validate_source) {
    struct {const char* check{};} w;
#include "copy_source_validation.inl"
    return true;
}
int main() {
    auto dll=LoadLibraryW(L"owner.dll");if(!dll)return 10;
    auto create=reinterpret_cast<Source*(*)()>(GetProcAddress(dll,"Create"));
    auto validate=reinterpret_cast<Validator>(GetProcAddress(dll,"Validate"));
    auto release=reinterpret_cast<void(*)(Source*)>(GetProcAddress(dll,"Release"));
    auto* source=create();
    // Actual production Membership sees a foreign callback table here.
    if(source->ValidateHeld(0,nullptr).ok() || !validate(0,nullptr,source))return 11;
    const bool okay=Prefix(0,nullptr,*source,validate);
    release(source);FreeLibrary(dll);
    return okay?0:1;
}
#endif
