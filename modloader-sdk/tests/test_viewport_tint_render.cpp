#include "viewport_tint.hpp"
#include "check.h"

namespace {
uint32_t rgba=0x22447740u;
uint64_t world=1024, now=500;
bool target_ready=true, watch_available=true, watch_current=true, lease_available=true;
bool full_width=true, lease_held=false, offline=true, mods_pending=false;
enum class Change { none, after_resolve, after_current, under_lease, online_under_lease };
Change change=Change::none;
unsigned renders=0, paints=0, commits=0;
menu_render::DirectColor paint_color{};

bool rect(void *,int x,int y,int width,int height,menu_render::DirectColor color) {
    CHECK_EQ(x,0);
    CHECK_EQ(y,0);
    CHECK_EQ(width,960);
    CHECK_EQ(height,540);
    ++paints;
    paint_color=color;
    return true;
}
bool publish(void *) {
    CHECK(lease_held);
    ++commits;
    return true;
}
}

extern "C" uint32_t lce_sdk_viewport_tint_v1() { return rgba; }
extern "C" uint64_t mc195_lucky_candidate_session(void *) { return world; }
extern "C" bool mc195_nbt_offline_ready() { return offline; }
bool mc195_mods::pending() { return mods_pending; }
extern "C" uint64_t sceKernelGetProcessTime() { return now; }
extern "C" int mc195_debug_pad_watch(int slot,uint64_t time,mc195_pad_watch *out) {
    CHECK_EQ(slot,0);
    CHECK_EQ(time,now);
    if (!watch_available) return 0;
    *out={};
    out->generation=1;
    return 1;
}
bool mc195_watch_view_single(const mc195_pad_watch *watch,uint64_t time,uint32_t width,uint32_t height,Mc195WatchView *out) {
    CHECK_EQ(watch->generation,1);
    CHECK_EQ(time,now);
    out->rectangle={0,0,full_width ? width : width/2,height};
    out->width=width;
    out->height=height;
    return true;
}
bool mc195_watch_view_single_current(const Mc195WatchView *,uint64_t) { return watch_current; }
bool mc195_watch_view_single_commit(const Mc195WatchView *,uint64_t,bool (*insert)(void *),void *context) {
    if (!lease_available) return false;
    CHECK(!lease_held);
    lease_held=true;
    if (change==Change::under_lease) world+=1024;
    if (change==Change::online_under_lease) offline=false;
    const bool inserted=insert(context);
    lease_held=false;
    return inserted;
}
bool menu_render::PostUiCompositor::target_ready() const noexcept { return ::target_ready; }
bool menu_render::PostUiCompositor::valid_owners(const CompositeOwner &,const PostUiOwner &) noexcept { return true; }
bool menu_render::PostUiCompositor::render_external(void *,const ExternalOverlay &effect) noexcept {
    ++renders;
    Mc195Viewport viewport{};
    if (!effect.resolve(effect.context,1920,1080,&viewport)) return false;
    if (change==Change::after_resolve) world+=1024;
    const DirectSink sink{nullptr,nullptr,rect,nullptr};
    if (!effect.paint(effect.context,sink,0,0,960,540) || !effect.current(effect.context,viewport)) return false;
    if (change==Change::after_current) world+=1024;
    return effect.commit(effect.context,viewport,publish,nullptr);
}

int main() {
    menu_render::PostUiCompositor compositor(nullptr,nullptr,{},{});
    auto render=[&](unsigned slot=0){ return mc195_viewport_tint::render(slot,compositor,nullptr); };
    CHECK(render());
    CHECK_EQ(commits,1);
    CHECK_EQ(paints,1);
    CHECK_EQ(paint_color.r,34);
    CHECK_EQ(paint_color.g,68);
    CHECK_EQ(paint_color.b,119);
    CHECK_EQ(paint_color.a,64);

    CHECK(!render(1));
    rgba=0x22447700u;
    CHECK(!render());
    rgba=0x22447761u;
    CHECK(!render());
    rgba=0x22447740u;
    target_ready=false;
    CHECK(!render());
    target_ready=true;
    now=0;
    CHECK(!render());
    now=500;
    world=0;
    CHECK(!render());
    world=1024;
    offline=false;
    CHECK(!render());
    offline=true;
    mods_pending=true;
    CHECK(!render());
    mods_pending=false;
    watch_available=false;
    CHECK(!render());
    watch_available=true;
    CHECK_EQ(renders,1);

    full_width=false;
    CHECK(!render());
    CHECK_EQ(paints,1);
    full_width=true;
    watch_current=false;
    CHECK(!render());
    watch_current=true;
    lease_available=false;
    CHECK(!render());
    lease_available=true;
    const Change changes[]={Change::after_resolve,Change::after_current,Change::under_lease,Change::online_under_lease};
    for (Change phase : changes) {
        change=phase;
        CHECK(!render());
    }
    CHECK_EQ(commits,1);
    CHECK(!lease_held);
    offline=true;
    change=Change::none;
    CHECK(render());
    CHECK_EQ(commits,2);
    TEST_MAIN_END("Viewport tint render");
}
