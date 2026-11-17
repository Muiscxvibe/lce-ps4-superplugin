#include "login_gui.hpp"
#include <assert.h>
#include <string.h>
using namespace lce_login_gui;
struct Mock { int action=-2;bool closed=true,pass=false,have_name=false,have_pass=false;bool remember=false,saved=false,used_token=false;int menus=0,connections=0; };
static bool show(void *p,bool n,bool s,bool r,bool){auto *m=(Mock *)p;m->have_name=n;m->have_pass=s;m->remember=r;++m->menus;return true;}
static int menu(void *p){auto *m=(Mock *)p;int a=m->action;m->action=-2;return a;}
static bool closed(void *p){return ((Mock *)p)->closed;}
static bool input(void *p,bool pass){((Mock *)p)->pass=pass;return true;}
static Input take(void *p,uint16_t *out,size_t n){auto *m=(Mock *)p;const char16_t *s=m->pass?u"password123":u"Codey";assert(n>12);while((*out++=*s++)){}return Input::Accepted;}
static bool load(void *p,char *name,size_t,char *token,size_t){auto *m=(Mock *)p;if(!m->saved)return false;strcpy(name,"Codey");memset(token,'a',43);token[43]=0;return true;}
static bool forget(void *p){((Mock *)p)->saved=false;return true;}
static bool connect(void *p,const char *name,const char *secret,bool token,bool remember){auto *m=(Mock *)p;assert(!strcmp(name,"Codey"));if(token)assert(strlen(secret)==43);else assert(!strcmp(secret,"password123"));m->used_token=token;assert(token || remember);++m->connections;return true;}
int main(){
 Mock m;Gui gui;Ops ops{&m,show,menu,closed,input,take,load,forget,connect};assert(gui.start(ops));
 m.action=2;gui.tick();gui.tick();assert(m.menus==2 && !m.connections);
 m.action=0;gui.tick();m.closed=false;gui.tick();assert(gui.state()==State::MenuClosing);m.closed=true;gui.tick();gui.tick();assert(m.have_name&&!m.have_pass);
 m.action=1;gui.tick();gui.tick();gui.tick();assert(m.have_name&&m.have_pass);
 m.action=3;gui.tick();gui.tick();assert(m.remember);
 m.action=2;gui.tick();gui.tick();assert(m.connections==1&&gui.state()==State::Done);
 m.saved=true;assert(gui.start(ops));assert(m.have_name&&m.have_pass&&m.remember);m.action=2;gui.tick();gui.tick();assert(m.used_token&&m.connections==2);
 assert(gui.start(ops));m.action=3;gui.tick();gui.tick();assert(!m.saved&&!m.remember&&!m.have_pass);m.action=-1;gui.tick();gui.tick();assert(gui.state()==State::Done);
}
