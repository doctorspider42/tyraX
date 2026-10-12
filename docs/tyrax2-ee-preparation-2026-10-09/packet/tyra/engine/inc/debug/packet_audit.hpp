#pragma once
#include <packet2.h>
namespace PacketAudit {
extern bool legacy;
extern unsigned checks, errors, culls, clips, replays;
void emitted(const qword_t*, unsigned, bool);
}
