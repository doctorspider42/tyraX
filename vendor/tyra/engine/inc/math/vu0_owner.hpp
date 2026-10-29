// Added by TyraX: shared micro-image identity across synchronous VU0 users.
#pragma once
namespace Tyra {
// Only access while VU0 is idle. An asynchronous caller owns the entire VU0
// register file and data memory until its explicit completion fence.
inline const void* vu0MicroOwner = nullptr;
} // namespace Tyra
