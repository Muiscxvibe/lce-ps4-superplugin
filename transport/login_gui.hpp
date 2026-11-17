#pragma once
#include <stddef.h>
#include <stdint.h>
namespace lce_login_gui {
enum class Input { Pending, Accepted, Cancelled };
enum class State { Idle, Menu, MenuClosing, Username, Password, Done };
struct Ops {
    void *context;
    bool (*show_menu)(void *,bool,bool,bool,bool);
    int (*take_menu)(void *); // -2 pending, -1 cancel, 0 username, 1 password, 2 connect, 3 save details
    bool (*menu_closed)(void *);
    bool (*start_input)(void *,bool);
    Input (*take_input)(void *,uint16_t *,size_t);
    bool (*load_saved)(void *,char *,size_t,char *,size_t);
    bool (*forget_saved)(void *);
    bool (*connect)(void *,const char *,const char *,bool,bool);
};
class Gui {
public:
    bool start(const Ops &ops);
    void tick();
    State state() const { return state_; }
private:
    bool open_menu();
    void finish();
    static bool convert(const uint16_t *in,char *out,size_t capacity);
    Ops ops_{}; State state_=State::Idle; int action_=-1;
    char username_[17]{},password_[257]{},token_[44]{};
    bool remember_=false;
};
}
