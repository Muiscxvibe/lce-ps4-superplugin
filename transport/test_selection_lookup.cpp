#include "selection_lookup.hpp"
#include <assert.h>
int main() {
    int first=1, second=2;
    void *items[]={&first,&second};
    lce_join_list::Vector list{nullptr,items,items+2,items+2};
    assert(lce_selection_lookup::record(&list,0)==&first);
    assert(lce_selection_lookup::record(&list,1)==&second);
    assert(!lce_selection_lookup::record(&list,-1));
    assert(!lce_selection_lookup::record(&list,2));
    assert(!lce_selection_lookup::record(nullptr,0));
    list.end=reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(items)-sizeof(void *));
    assert(!lce_selection_lookup::record(&list,0));
    list.end=reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(items)+1);
    assert(!lce_selection_lookup::record(&list,0));
    list.end=reinterpret_cast<void **>(reinterpret_cast<uintptr_t>(items)+1025*sizeof(void *));
    assert(!lce_selection_lookup::record(&list,0));
}
