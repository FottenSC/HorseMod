#include "deterministic/ReplayCaptureAccounting.hpp"
#include <cstdlib>
namespace ReplayCaptureAccountingTest {
inline void run() {
    using Ledger=Horse::Deterministic::ReplayCaptureAccounting;
    const auto check=[](bool ok){if(!ok) std::abort();};
    Ledger ledger;Ledger::Reservation a,b,c;
    const Ledger::Sources sources{1,2,3,4,5,6};
    auto other=sources;other[5]=7;
    check(!ledger.Register(a,sources,100,60,99) && ledger.bytes()==0);
    check(ledger.Register(a,sources,100,60,100) && ledger.bytes()==100);
    check(!ledger.Register(a,sources,100,60,100));
    check(!ledger.Register(b,sources,100,60,39));
    check(ledger.Register(b,sources,100,60,40) && ledger.bytes()==140);
    // Partial identity matches or different storage extents share nothing.
    check(!ledger.Register(c,other,100,60,40));
    check(!ledger.Register(c,sources,100,59,40));
    check(ledger.Register(c,other,100,60,100) && ledger.bytes()==240);
    // A failed native retirement does not call Retire; its full charge stays.
    check(ledger.bytes()==240);
    bool destroyed=false;
    check(ledger.Retire(a,[&]() noexcept {destroyed=true;}) && destroyed && ledger.bytes()==200);
    // The remaining shared owner now bears the source charge. Retiring the
    // first owner must never credit source storage still held by the second.
    check(!ledger.Retire(a,[]() noexcept {}));
    check(ledger.Retire(b,[]() noexcept {}) && ledger.bytes()==100);
    check(ledger.Retire(c,[]() noexcept {}) && ledger.bytes()==0);
    auto invalid=sources;invalid[0]=0;
    check(!ledger.Register(a,invalid,100,60,100));
    const auto maximum=std::numeric_limits<std::size_t>::max();
    check(ledger.Register(a,sources,maximum-1,1,maximum));
    check(ledger.Register(b,other,100,1,100) && ledger.bytes()==maximum);
    check(ledger.Retire(a,[]() noexcept {}) && ledger.bytes()==100);
    check(ledger.Retire(b,[]() noexcept {}) && ledger.bytes()==0);
    // Immutable private images may be shared only by exact retained COM
    // allocation identity and extent. Each remaining owner inherits the
    // charge when an earlier checkpoint retires.
    const Ledger::Images images{{{20,16},{21,8},{22,16},{23,8},{24,4},{25,4}}};
    auto revised=images;revised[0].identity=30;revised[1].identity=31;
    check(ledger.Register(a,sources,128,60,128,{},images));
    check(!ledger.Register(b,sources,128,60,35,{},revised));
    check(ledger.Register(b,sources,128,60,36,{},revised) && ledger.bytes()==164);
    check(ledger.Retire(a,[]() noexcept {}) && ledger.bytes()==128);
    check(ledger.Retire(b,[]() noexcept {}) && ledger.bytes()==0);
    auto malformed=images;malformed[0].bytes=0;
    check(!ledger.Register(a,sources,128,60,128,{},malformed));
    check(!ledger.Register(a,sources,115,60,128,{},images));
    // Native history images are shared individually; private snapshot images
    // and the complete undo reserve stay charged in gross.
    const Ledger::Histories histories{{{10,8},{11,4},{12,1}}};
    auto partial=histories;partial[1].identity=13;
    check(ledger.Register(a,sources,100,60,100,histories));
    check(!ledger.Register(b,sources,100,60,26,histories));
    check(ledger.Register(b,sources,100,60,27,histories) && ledger.bytes()==127);
    check(!ledger.Register(c,sources,100,60,30,partial));
    check(ledger.Register(c,sources,100,60,31,partial) && ledger.bytes()==158);
    // Failed retirement retains all identities. Successful retirement hands
    // their charge to remaining owners, even with partial history sharing.
    check(ledger.bytes()==158);
    check(ledger.Retire(b,[]() noexcept {}) && ledger.bytes()==131);
    check(ledger.Retire(a,[]() noexcept {}) && ledger.bytes()==100);
    check(ledger.Retire(c,[]() noexcept {}) && ledger.bytes()==0);
    auto invalid_history=histories;invalid_history[0].identity=0;
    check(!ledger.Register(a,sources,100,60,100,invalid_history));
    invalid_history=histories;invalid_history[0].bytes=0;
    check(!ledger.Register(a,sources,100,60,100,invalid_history));
    check(!ledger.Register(a,sources,70,60,100,histories));
    check(ledger.Register(a,sources,100,60,100,histories));
    auto changed_extent=histories;changed_extent[0].bytes=9;
    check(!ledger.Register(b,sources,100,60,34,changed_extent));
    check(ledger.Register(b,sources,100,60,35,changed_extent) && ledger.bytes()==135);
    check(ledger.Retire(a,[]() noexcept {}) && ledger.bytes()==100);
    check(ledger.Retire(b,[]() noexcept {}) && ledger.bytes()==0);
}
}
