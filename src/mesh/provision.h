#pragma once
#include <stddef.h>
#include <stdint.h>

// Device-to-device profile sync (Wio Tracker L1). One device shares its config
// (everything but the identity) over a dedicated BLE service; the other scans,
// connects to the nearest sharer, and applies it. Provisioning owns Bluefruit
// exclusively, so it runs as a reboot-into mode (see provision_wio.cpp) rather
// than alongside the companion BLE.

namespace provision {

enum class Mode  : uint8_t { None, Share, Receive };
enum class State : uint8_t { Idle, Advertising, Connected, Sending, Scanning, Receiving, Done, Error };

Mode  pending();        // read the persisted request flag
void  request(Mode m);  // persist the flag, then reboot into provisioning
void  begin(Mode m);    // boot-time entry: claims Bluefruit and starts the role
void  loop();           // pump the active role
void  reboot();         // leave provisioning mode (back to a normal boot)
State state();
int   progress();       // 0..100 for the current transfer
int   bytes_done();     // bytes sent (Share) or received (Receive) so far
int   bytes_total();    // total payload bytes for the current transfer

// Receiver device picker: rather than auto-connecting to the strongest signal,
// the scan collects nearby advertisers so the user can choose one. device_*()
// read the live list; connect_to() stops the scan and pulls from that device.
int          device_count();
const char*  device_name(int i);
int          device_rssi(int i);
bool         device_has_service(int i);   // true if it advertises the Provision service
void         connect_to(int i);

} // namespace provision
