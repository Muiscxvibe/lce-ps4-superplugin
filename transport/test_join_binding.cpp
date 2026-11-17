#include "join_binding.hpp"
#include <cassert>
#include <cstdio>
static lce_join_list::Vector list{};
static int calls[3];
static lce_join_list::Vector *native_list(void *, int a, int b, bool c) { assert(a==1 && b==2 && !c); ++calls[0]; return &list; }
static int native_join(void *, void *, int a, int b) { assert(a==3 && b==4); ++calls[1]; return 7; }
static void native_info(void *, void *, lce_join_adapter::Complete cb, void *ctx) { ++calls[2]; cb(true,ctx); }
static void complete(bool ok, void *ctx) { *static_cast<bool *>(ctx)=ok; }
int main() {
 lce_servers::List servers{}; lce_join_adapter::Ops ops{}; ops.native_list=native_list; ops.native_join=native_join; ops.native_full_info=native_info;
 int manager=0,other=0; lce_join_hook::Binding a(&manager,servers,ops),b(&other,servers,ops);
 assert(a.activate() && !b.activate()); auto h=a.replacements();
 auto l=reinterpret_cast<lce_join_list::Vector *(*)(void *,int,int,bool)>(h.list);
 auto j=reinterpret_cast<int (*)(void *,void *,int,int)>(h.join);
 auto f=reinterpret_cast<void (*)(void *,void *,lce_join_adapter::Complete,void *)>(h.full_info);
 assert(l(&manager,1,2,false)==&list && l(&other,1,2,false)==&list);
 unsigned char row[32]={}; assert(j(&manager,row,3,4)==7 && j(&other,row,3,4)==7);
 bool done=false; f(&manager,row,complete,&done); assert(done); done=false; f(&other,row,complete,&done); assert(done);
 assert(calls[0]==2 && calls[1]==2 && calls[2]==2); assert(a.deactivate() && b.activate() && b.deactivate());
 puts("join binding tests passed");
}
