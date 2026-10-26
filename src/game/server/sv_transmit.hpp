// sv_transmit.hpp - which players are sent to the (local) client: dormancy
//
// Source only networks entities that are in the client's PVS (CServerGameEnts::CheckTransmit). Players that are
// not sent go *dormant* on the client: they are not drawn, the client keeps their last received state and cheats
// lose them (dormant ESP shows the last known spot / heard sounds for a few seconds).
#pragma once

void SV_UpdateTransmit();     // every server tick, after the players moved
