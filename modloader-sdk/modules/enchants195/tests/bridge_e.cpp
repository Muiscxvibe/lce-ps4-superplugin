// Host bridge for enchants195 (linked with ../strings195/tests/bridge.cpp).
#include "../enchants195.h"
extern "C" {
s195_env *bridge_s195_env(void);
int bridge_e195_publish(const e195_spec *s, unsigned n, e195_record *out) { return e195_publish(bridge_s195_env(), s, n, out); }
int bridge_e195_verify(const e195_record *r) { return e195_verify(bridge_s195_env(), r); }
int bridge_e195_clone(const char *k, char *err, unsigned cap) { return e195_clone_from_key(k, err, cap); }
}
