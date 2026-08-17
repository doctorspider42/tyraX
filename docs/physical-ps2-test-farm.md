# Physical PS2 test farm

A physical PS2 test farm is a proposed service that leases one of several real
consoles to a remote developer or agent, runs a TyraX build on it, and returns
logs, input control and captures without exposing the console network itself.

This page is a design proposal, not an implemented TyraX feature. It records the
architecture, the constraints already known from the current ps2link path, and
the smallest sequence of experiments that can prove the idea.

## The outcome

One host PC owns a set of console **slots**. A remote caller asks for a slot,
uploads a built artifact, starts it, consumes its telemetry, and releases it.
The service grants a time-limited lease rather than handing out a PS2 IP
address. Every ps2link connection stays on a private, low-latency LAN beside the
consoles.

```text
remote human / agent
        |
        | authenticated HTTPS
        v
Cloudflare Access + Tunnel
        |
        v
PS2 farm daemon ----- artifact store / lease database
        |
        +-- slot 1 worker -- ps2client channel -- PS2 1
        +-- slot 2 worker -- ps2client channel -- PS2 2
        +-- slot N worker -- ps2client channel -- PS2 N
        |
        +-- isolated reset-button controller
        +-- individually switched AC outlets
```

The public boundary is the farm API. TCP 18193, UDP 18194 and the consoles'
private addresses are never published to the Internet.

## Goals

- Run independent TyraX builds concurrently on several physical consoles.
- Let a human, CI job or coding agent reserve and release one console without
  coordinating manually with other users.
- Keep the existing ps2link `host:` filesystem on the farm PC, where its many
  synchronous round trips remain LAN-local.
- Recover automatically from a stopped game, a quiet Tyra assert, a dead
  ps2client, a wedged ps2link and a console left in standby.
- Expose the existing logs, Remote Pad, Live Debugger heartbeat and game-owned
  screenshot through one authenticated lease.
- Attribute every file operation, log line, input command and recovery action
  to exactly one console and lease.

## Non-goals

- Sandboxing arbitrary PS2 code. A leased ELF has full control of the console.
- Making ps2link itself an Internet protocol.
- Serving `host:` files from the remote caller's workstation.
- Continuous video streaming in the first version.
- Scheduling Docker builds on the farm in the first version. The smallest
  useful artifact is an already-built game bundle.

## One Ethernet switch is enough

All consoles and the host PC can share one ordinary Ethernet switch. Every PS2
gets a unique static address through its own `IPCONFIG.DAT`; ps2link has no
DHCP. A single host NIC can carry every console session, although a dedicated
NIC is preferable so the PS2 subnet can be isolated from the host's Internet
connection.

A concrete layout for an initial rack could be:

```text
farm host: 10.77.0.1/24
PS2 1:     10.77.0.101
PS2 2:     10.77.0.102
PS2 N:     10.77.0.100+N
```

The switch should sit on a dedicated VLAN or physical network. The host routes
nothing from that network by default. Firewall rules allow each console to talk
to the farm host and prevent it from reaching the home/office LAN or the
Internet. A managed switch is useful for per-port diagnostics and isolation but
is not required for the first two-console experiment.

The PS2's Ethernet bandwidth is not the first scaling limit. Builds and
artifacts are prepared before deployment, and ps2link's synchronous request/
response protocol is latency-bound long before a modern switch or SSD is full.

## The current ps2client is the first concurrency constraint

The present Runner deliberately allows only one ps2client file server on a PC;
see [One file server at a time](ps2link-setup.md#one-file-server-at-a-time).
That rule is correct for today's process model but is not a hardware limit.

ps2client opens three channels:

| Channel | Shape | Multi-console consequence |
|---|---|---|
| File requests | one TCP connection from the PC to each console on 18193 | Naturally independent per console. |
| Commands | connected UDP from the PC to each console on 18194 | Independent outgoing sockets can coexist. |
| Console log | local UDP listener on `0.0.0.0:18194` | A second stock ps2client cannot bind the same local port. |

The conflicting local log listener is the immediate blocker, not the TCP file
server. The first implementation should therefore split log collection from
per-console file serving:

1. Start one worker per console with its own TCP `host:` session and artifact
   root, but no private UDP log listener.
2. Run one UDP 18194 collector for the whole host.
3. Receive with `recvfrom`, preserve the sender address and route every line to
   the slot whose PS2 IP sent it.
4. Let the lease supervisor decide liveness from that attributed stream.

Using socket reuse between unmodified clients is not sufficient: delivery may
be distributed between sockets, while each process currently discards the
sender address. One collector must own the datagrams.

The production shape can be a single multi-console farm daemon containing the
ps2client protocol, or a supervisor around a small farm-specific ps2client
mode. The two-console experiment should decide which is less invasive. In both
shapes the existing Runner's global process-ownership rule remains the default
for normal editor sessions; farm operation is an explicit separate mode.

## A slot is more than an IP address

Each physical slot has stable metadata and four resources:

- console identity: name, static IP, model/revision and capabilities;
- one exclusive ps2client file/command session;
- one reset-button actuator channel;
- one individually controllable AC outlet.

Optional metadata includes USB HID support, video region, memory-card image,
capture input and known hardware quirks. A lease selects by capability rather
than assuming every PS2 revision is interchangeable.

The daemon owns a state machine such as:

```text
offline -> recovering -> ready -> leased -> deploying -> running
                        ^                         |
                        +---- releasing <--------+

any failed recovery -> quarantined
```

Only `ready` slots can be leased. A lease contains an unpredictable identifier,
an owner, a short-lived access token, a TTL, a slot lock and an isolated job
directory. Expiration follows the same release and recovery path as an explicit
release; it must never merely forget the database row and leave the game alive.

## Keep `host:` beside the console

The remote caller builds locally and uploads a bundle containing the ELF and
the complete runtime tree that ps2client must serve. The farm unpacks it into a
new lease directory, validates paths against traversal, identifies the ELF and
starts the worker with that directory as its filesystem root.

The worker then performs the normal sequence locally:

1. reset the selected console;
2. wait for the TyraX ps2link banner/liveness signal;
3. run `execee host:<game>.elf`;
4. remain attached as that game's only file server;
5. stream attributed console output and lease events to the API.

Serving `host:` from the caller through a VPN or Cloudflare would put every
small request/reply across WAN latency. It also makes a caller disconnect a
console-wide filesystem failure. Uploading once and serving locally keeps that
failure inside the slot worker.

Later versions may accept a repository revision and build it on the farm, but
that adds Docker scheduling, source credentials and build-cache isolation to a
problem that does not need them for its first proof.

## Hardware recovery: press the real button

Network recovery remains the normal path. The TyraX ps2link r6 reset path is
already designed to stop a running game and re-execute ps2link; the physical
path is the fallback for code that destroys the IOP, the network stack or the
resident loader.

Cutting and restoring AC alone is insufficient because a PS2 returns to
standby. Each slot therefore needs an electrically isolated dry contact wired
in parallel with the console's front Power/Reset switch. A reed relay, signal
relay or PhotoMOS can imitate the button without injecting any voltage into the
console. A USB-connected microcontroller can expose one named output per slot.
Fat and slim revisions have different button boards, so every harness needs its
pinout measured and documented; the controller must close contacts only, never
share an assumed logic voltage.

The button's normal semantics make recovery converge:

- a short closure turns on a console in standby;
- the same short closure resets a running console;
- a closure longer than one second requests standby.

The full recovery ladder is:

1. **Protocol reset:** send ps2link `reset`; wait for the banner, not merely a
   successful UDP send or an ICMP ping.
2. **Front-button reset:** close Power/Reset for roughly 200 ms; wait for the
   memory-card auto-boot to reach ps2link.
3. **Standby round trip:** hold the contact for roughly 1.5-2 seconds, release,
   then issue a short closure to turn the console on again.
4. **Cold cycle:** switch that slot's certified AC outlet off for 5-10 seconds,
   restore it, wait for standby power, then issue a short closure.
5. **Quarantine:** after a bounded number of attempts, stop cycling and require
   inspection.

The exact delays must be measured on every supported revision rather than
treated as protocol constants. An optional optically isolated sensor placed on
the red/green power LED gives the supervisor an independent standby/on signal.
It is useful evidence, but the final readiness check is still a responding
ps2link session.

Use a certified, individually switched PDU or smart outlet for mains power. The
custom board should control only the low-voltage button contacts. A mechanical
button-pushing actuator is acceptable for a no-solder prototype, but it is not
a reliable rack design.

## The public API leases a console, not its network

Cloudflare Tunnel can publish one HTTPS service without opening inbound ports
on the farm. Cloudflare Access can authenticate humans through an identity
provider and unattended agents through service credentials. Neither component
needs to understand ps2link.

An initial API can stay deliberately small:

```text
POST   /leases                       reserve a compatible ready slot
PUT    /leases/{id}/artifact         upload the runtime bundle
POST   /leases/{id}/run              reset, attach and execute
GET    /leases/{id}/events           logs and state over SSE/WebSocket
POST   /leases/{id}/pad              drive Remote Pad
POST   /leases/{id}/capture          request the game-owned screenshot
POST   /leases/{id}/reset            restart within the same lease
POST   /leases/{id}/renew            extend the TTL within policy
DELETE /leases/{id}                  release and recover the slot
```

The API token is scoped to one lease. The daemon checks that scope on every
operation; knowing a slot name or internal IP grants nothing. Human and agent
identities, lease transitions, artifact hashes and all recovery steps go into
an audit log.

Raw ps2link access can be offered later through a private-network product if a
real use case needs it, but it should not be the default. ps2link has no
Internet-facing authentication boundary, and direct access would let the
client become the console's fragile file server.

## Feedback without a capture card

TyraX already has enough hardware-facing channels for an automated first
version:

- ps2link forwards `printf` and `TYRA_LOG` output;
- the Live Debugger snapshot is a heartbeat and exposes game state;
- Remote Pad drives the running game's controller without window focus;
- the game-owned screenshot reads the real GS frame and writes it through
  `host:`.

The farm API can proxy those existing operations. A capture card is optional
for the MVP and becomes valuable for failures before the game runtime exists:
BIOS, FreeMcBoot, the ps2link boot screen and a completely dead network. A
single switched diagnostic capture input may be enough before dedicating one
capture device to every console.

Do not use ping as the health signal. A console can answer ICMP while its EE or
deploy path is unusable. Readiness means that a reset/deploy exchange produced
the expected ps2link output, and running health means that the selected runtime
channel advances.

## Trust and isolation

Cloudflare authentication protects the farm service; it does not sandbox the
ELF. Arbitrary PS2 code can overwrite memory, reset processors, erase or corrupt
the memory card and originate network traffic. The first service should
therefore be restricted to trusted users and treat each console as recoverable
lab equipment.

Practical containment:

- keep the PS2 subnet isolated and block all egress except the farm host;
- never forward ps2link ports from the public Internet;
- clone and label the boot memory cards so a damaged one is replaceable;
- keep no irreplaceable saves or HDD data in a farm console;
- impose artifact-size, lease-duration and reset-rate limits;
- unpack archives without symlinks or path traversal;
- remove every job directory after release according to an explicit retention
  policy;
- let only the daemon operate the PDU and reset controller;
- quarantine a slot whose observed state disagrees with the supervisor.

This is containment, not adversarial multi-tenancy. Supporting untrusted public
users would require a separate threat model and probably disposable boot media.

## Implementation path

Start with two consoles. A single console does not exercise the one constraint
that makes this different from today's F6 path.

### Phase 0: one-slot vertical slice

- Reserve one slot through a local-only API.
- Upload and unpack one built game bundle.
- Deploy it with the existing ps2client path.
- Stream logs, drive Remote Pad and request a game-owned screenshot.
- Release on command and on TTL expiry.

### Phase 1: prove concurrent ps2client sessions

- Add the shared UDP log collector.
- Run two independent TCP file servers against two consoles at once.
- Deploy different games with overlapping filenames.
- Prove file requests and log lines never cross slots.
- Kill one worker and prove the other session continues.

This is the decisive experiment. Do it before building the public API or a
large scheduler.

### Phase 2: physical recovery

- Add two isolated Power/Reset contacts and two switched AC outlets.
- Exercise the whole recovery ladder against a quiet assert, an infinite loop,
  a killed ps2client, a dead network path and a console already in standby.
- Measure boot and recovery timings for the exact console revisions.
- Quarantine after bounded failures.

### Phase 3: authenticated remote service

- Put the HTTPS API behind Cloudflare Tunnel and Access.
- Add separate human and agent authentication policies.
- Stream events, enforce lease-scoped tokens and record audit events.
- Test client disconnects, expired leases and daemon restarts.

### Phase 4: integration

- Add a TyraX CLI that builds locally, creates a lease, uploads the bundle and
  follows its event stream.
- Expose slot capabilities in the editor without making farm configuration part
  of a project file.
- Add optional diagnostic video capture only after the native telemetry path is
  proven insufficient.

## Acceptance criteria for the first useful farm

- Two consoles run different projects concurrently for at least one hour.
- Every UDP log line is attributed to the correct source IP and lease.
- Every `host:` request is confined to its lease directory.
- Releasing or expiring one lease cannot stop the other console's file server.
- A quiet Tyra assert is detected and recovered without human intervention.
- A game that destroys the network path is recovered through the front button.
- An AC cycle returns through standby and the automated button pulse into
  ps2link.
- A failed recovery becomes `quarantined` instead of cycling forever.
- A remote caller never needs direct access to a PS2 address or ps2link port.
- Expired credentials cannot read logs, drive input or operate hardware.

## Open decisions

- Extend ps2client with a farm mode, or move its protocol into the daemon?
- Linux appliance or Windows service? Linux simplifies long-running workers and
  network namespaces; Windows matches many existing TyraX developer machines.
- Which reset controller protocol gives each channel a stable identity after a
  USB reconnect?
- Is one switched capture input enough, or does each slot eventually need
  continuous video?
- Should the first artifact format be the whole `bin/` tree or a manifest with
  content-addressed assets?
- Which console revisions and ps2link builds are valid scheduling
  capabilities?

Those decisions can wait until the two-console concurrency and recovery
experiments have produced evidence. Ethernet wiring and Cloudflare are the easy
edges; multiplexing ps2client correctly and making recovery converge are the
core of the project.

## References

- [Running and debugging on a real PS2](ps2link-setup.md)
- [Remote Pad](remote-pad.md)
- [The devkit and the game-owned screenshot](devkit.md)
- [ps2client source](https://github.com/ps2dev/ps2client)
- [Cloudflare Tunnel routing](https://developers.cloudflare.com/tunnel/routing/)
- [Cloudflare Access service tokens](https://developers.cloudflare.com/cloudflare-one/access-controls/service-credentials/service-tokens/)
- [Sony's Power/Reset button behaviour](https://helpguide.sony.net/gbmig/ADL31001/egb/5-2P/tvcfront_ps2_aep_52p.html)
