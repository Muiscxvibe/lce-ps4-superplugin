#include "login_gui.hpp"
#include "auth_client.h"
namespace lce_login_gui {
bool Gui::start(const Ops &ops) {
    if ((state_!=State::Idle && state_!=State::Done) || !ops.show_menu || !ops.take_menu ||
        !ops.menu_closed || !ops.start_input || !ops.take_input || !ops.load_saved || !ops.forget_saved || !ops.connect) return false;
    lce_auth_wipe(username_,sizeof username_);lce_auth_wipe(password_,sizeof password_);
    lce_auth_wipe(token_,sizeof token_);ops_=ops;remember_=ops_.load_saved(ops_.context,username_,sizeof username_,token_,sizeof token_);
    if(!remember_){lce_auth_wipe(username_,sizeof username_);lce_auth_wipe(token_,sizeof token_);}
    action_=-1;state_=State::Idle;return open_menu();
}
bool Gui::open_menu() {
    if (!ops_.show_menu(ops_.context,username_[0]!=0,password_[0]!=0 || token_[0]!=0,remember_,token_[0]!=0)) {finish();return false;}
    state_=State::Menu;return true;
}
void Gui::finish() {
    lce_auth_wipe(username_,sizeof username_);lce_auth_wipe(password_,sizeof password_);
    lce_auth_wipe(token_,sizeof token_);remember_=false;action_=-1;state_=State::Done;
}
bool Gui::convert(const uint16_t *in,char *out,size_t capacity) {
    size_t n=0;
    for (size_t i=0;i<256 && in[i];++i) {
        uint32_t c=in[i];
        if (c>=0xd800 && c<=0xdbff) {
            if (++i>=256 || in[i]<0xdc00 || in[i]>0xdfff)return false;
            c=0x10000+((c-0xd800)<<10)+(in[i]-0xdc00);
        } else if (c>=0xdc00 && c<=0xdfff) return false;
        if (c<0x20)return false;
        unsigned count=c<0x80?1:c<0x800?2:c<0x10000?3:4;
        if (n+count>=capacity)return false;
        if (count==1)out[n++]=(char)c;
        else {out[n++]=(char)((count==2?0xc0:count==3?0xe0:0xf0)|(c>>(6*(count-1))));
            for(unsigned j=count-1;j;--j)out[n++]=(char)(0x80|((c>>(6*(j-1)))&0x3f));}
    }
    out[n]=0;return true;
}
void Gui::tick() {
    if (state_==State::Menu) {
        int result=ops_.take_menu(ops_.context);if(result==-2)return;
        action_=result;state_=State::MenuClosing;return;
    }
    if (state_==State::MenuClosing) {
        if(!ops_.menu_closed(ops_.context))return;
        if(action_<0 || action_>3){finish();return;}
        if(action_==3) {
            if(!remember_)remember_=true;
            else if(ops_.forget_saved(ops_.context)){remember_=false;lce_auth_wipe(token_,sizeof token_);}
            open_menu();return;
        }
        if(action_==2) {
            const bool use_token=remember_ && token_[0];
            const char *secret=use_token?token_:password_;
            if(username_[0] && secret[0] && ops_.connect(ops_.context,username_,secret,use_token,remember_ && !use_token))finish();
            else open_menu();
            return;
        }
        const bool password=action_==1;
        if(!ops_.start_input(ops_.context,password)){open_menu();return;}
        state_=password?State::Password:State::Username;return;
    }
    if (state_==State::Username || state_==State::Password) {
        uint16_t text[257]={};Input result=ops_.take_input(ops_.context,text,257);
        if(result==Input::Pending)return;
        const bool pass=state_==State::Password;
        if(result==Input::Accepted) {
            char value[257]={};bool ok=convert(text,value,pass?sizeof password_:sizeof username_);
            size_t n=0;while(ok && value[n])++n;
            if(pass)ok=ok && n>=8;
            else {
                ok=ok && n>=3 && n<=16;
                for(size_t i=0;ok && i<n;++i) {char c=value[i];ok=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}
            }
            if(ok) {lce_auth_wipe(token_,sizeof token_);if(!pass)lce_auth_wipe(password_,sizeof password_);char *dest=pass?password_:username_;size_t cap=pass?sizeof password_:sizeof username_;lce_auth_wipe(dest,cap);for(size_t i=0;i<=n;++i)dest[i]=value[i];}
            lce_auth_wipe(value,sizeof value);
        }
        lce_auth_wipe(text,sizeof text);open_menu();
    }
}
}
