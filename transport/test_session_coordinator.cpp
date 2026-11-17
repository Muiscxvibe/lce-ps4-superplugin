#include "session_coordinator.hpp"
#include <assert.h>

int main() {
    using lce_session_coordinator::supports_player_request;
    for(int user=0;user<4;++user) {
        assert(supports_player_request(1<<user,user));
        assert(!supports_player_request((1<<user)|(1<<((user+1)%4)),user));
        assert(!supports_player_request(0,user));
    }
    assert(!supports_player_request(1,-1));
    assert(!supports_player_request(1,4));
}
