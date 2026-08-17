<p align="center">
  <img src="/loosehose/DutchOven/raw/refs/heads/main/assets/dutchoven.png" alt="DutchOven red cooking pot logo" width="520">
</p>

<h1 align="center">DutchOven</h1>

<p align="center">
  <strong>Application-scoped Windows network brownouts for deterministic red-team validation.</strong>
</p>

<p align="center">
  Native C &nbsp;•&nbsp; Windows Filtering Platform &nbsp;•&nbsp; IPv4 + IPv6 &nbsp;•&nbsp; Dynamic cleanup
</p>

<p align="center">
  <a href="#quick-start">Quick start</a> •
  <a href="#operation-profiles">Profiles</a> •
  <a href="#cli-reference">CLI</a> •
  <a href="#how-it-works">How it works</a> •
  <a href="#build">Build</a> •
  <a href="#beacon-object-file">BOF</a> •
  <a href="#validation-workflow">Validation</a>
</p>

> [!CAUTION]
> **Controlled environments only.** Run DutchOven only on systems you own or are explicitly
> authorized to assess. It changes live Windows network policy and requires an elevated
> Administrator token. Review [SECURITY.md](SECURITY.md) before use.

## What is DutchOven?

DutchOven is a deliberately small Windows red-team primitive that places explicit executable paths
behind a deterministic network gate. During each period, matching applications are blocked for a
bounded interval and allowed to pass for the remainder.

The result is a repeatable **brownout**, not a service stop or permanent firewall rule. It creates a
controlled way to measure retry logic, buffering, health-state transitions, delayed delivery, and
recovery behavior while a process remains alive.

| Design choice | Operator value |
| --- | --- |
| Application-scoped WFP filters | Limits the experiment to paths supplied by the operator |
| Timed block/pass duty cycle | Produces measurable degradation instead of only online/offline states |
| Atomic IPv4 and IPv6 changes | Keeps both address families on the same schedule |
| Dynamic WFP session | Removes filters and the generated sublayer when the engine session closes |
| Native C implementation | No Go, .NET, service installation, or custom driver dependency |
| Explicit targets only | No product enumeration, process guessing, or embedded target database |

## Quick start

Open an **elevated PowerShell** window.

Validate the resolved target and timing without changing WFP:

```powershell
.\dutchoven.exe --app 'C:\Path\To\Target.exe' --dry-run
```

Run the default brownout profile:

```powershell
.\dutchoven.exe --app 'C:\Path\To\Target.exe'
```

That is the complete minimum invocation. `--app` is the only required flag. The default profile
blocks the target for 1.5 seconds in every 5-second period and exits after one minute.

Use `Ctrl+C` to stop early. DutchOven removes active filters before closing its WFP session; the
dynamic session is the cleanup backstop if the process is interrupted.

## Operation profiles

Profiles bake in useful schedules so operators do not need to specify every timing value.

| Profile | Block | Pass | Period | Duration | Intended use |
| --- | ---: | ---: | ---: | ---: | --- |
| `light` | 0.5 s | 4.5 s | 5 s | 60 s | Low-impact retry observation |
| `brownout` | 1.5 s | 3.5 s | 5 s | 60 s | Balanced default experiment |
| `heavy` | 3.5 s | 1.5 s | 5 s | 60 s | Queue and recovery-pressure testing |
| `blackout` | 5 s | 0 s | 5 s | 30 s | Short, bounded full interruption |

Select a profile with one optional flag:

```powershell
.\dutchoven.exe --app 'C:\Path\To\Target.exe' --profile heavy
```

## CLI reference

```text
dutchoven --app <absolute.exe> [--app <absolute.exe> ...] [options]
```

| Flag | Required | Description |
| --- | :---: | --- |
| `--app <absolute.exe>` | Yes | Target executable path; repeat for multiprocess applications |
| `--profile <name>` | No | `light`, `brownout`, `heavy`, or `blackout` |
| `--period-ms <ms>` | No | Override the complete block/pass period |
| `--block-ms <ms>` | No | Override the blocked portion of each period |
| `--duration-ms <ms>` | No | Override the bounded total runtime |
| `--warmup-ms <ms>` | No | Allow traffic before the first block interval |
| `--dry-run` | No | Validate and print configuration without opening WFP |
| `--help` | No | Print command help |

Advanced timing flags override the selected profile regardless of argument order:

```powershell
.\dutchoven.exe `
  --app 'C:\Path\To\Target.exe' `
  --period-ms 8000 `
  --block-ms 2000 `
  --duration-ms 120000 `
  --warmup-ms 5000
```

Timing limits are enforced by the parser:

- Period: `100–60000 ms`
- Block: `1 ms` through the complete period
- Duration: at least one period, at most `900000 ms` (15 minutes)
- Warmup: at most `60000 ms`
- Targets: at most 16 explicit local executable paths

## How it works

```text
explicit executable path
          │
          ▼
   WFP application ID
          │
          ├── ALE_AUTH_CONNECT_V4 ──┐
          └── ALE_AUTH_CONNECT_V6 ──┤── BLOCK / PASS schedule
                                    │
                                    └── dynamic WFP session cleanup
```

1. DutchOven resolves each supplied executable into a WFP application identity.
2. It opens a non-persistent, dynamic WFP engine session and creates a generated sublayer.
3. At the beginning of a block interval, IPv4 and IPv6 filters are added in one transaction.
4. At the beginning of a pass interval, those filters are removed in one transaction.
5. At completion or normal interruption, DutchOven removes any active filters and closes the engine.

The console prints state transitions with wall-clock and schedule-relative timestamps:

```text
Targets: 1
  C:\Path\To\Target.exe
Schedule: block=1500ms pass=3500ms period=5000ms duration=60000ms warmup=0ms
Profile: brownout
Mode: brownout
BLOCK cycle=1 filters=2 wall_ms=1786973903811 elapsed_ms=0
PASS cycle=1 wall_ms=1786973908813 elapsed_ms=5000
...
CLEAN filters=0 session=closed cycles=12 wall_ms=1786973963812 elapsed_ms=60000
```

## Build

The default Linux build exercises parsing and dry-run behavior. The live WFP gate requires the
native Windows cross-build.

```sh
make test
make windows
```

Primary outputs:

| Artifact | Purpose |
| --- | --- |
| `build/dutchoven` | POSIX parser and dry-run binary |
| `build/dutchoven_tests` | Core unit tests |
| `build-windows/dutchoven.exe` | Native Windows WFP gate |
| `build-windows/dutchoven_tests.exe` | Native Windows test binary |
| `bof/build/dutchoven.x64.o` | x64 Beacon Object File |

The Windows executable links only against Windows system libraries: `fwpuclnt`, `rpcrt4`, and
`advapi32`.

## Beacon Object File

The BOF packages DutchOven as a short, synchronous pulse that runs inline in an x64 Beacon. Build
the COFF object and verify its import contract with:

```sh
make bof
```

Load `bof/dutchoven.cna` in Cobalt Strike's Script Manager, then invoke it from a Beacon console:

```text
dutchoven "C:\Path\To\Target.exe"
dutchoven "C:\Path\To\Target.exe" heavy
```

The default profile is `brownout`. Profiles map to a single bounded block interval:

| Profile | Pulse |
| --- | ---: |
| `light` | 0.5 s |
| `brownout` | 1.5 s |
| `heavy` | 3.5 s |
| `blackout` | 5 s |

Each invocation opens a dynamic WFP session, atomically installs IPv4 and IPv6 connect filters,
waits for the selected pulse, explicitly removes both filters, and closes the session before
returning. Closing the dynamic session remains the cleanup backstop if explicit removal fails.

The BOF uses Dynamic Function Resolution for `Kernel32`, `Rpcrt4`, and `Fwpuclnt`; it has no C
runtime imports. It accepts one existing absolute executable path, requires an elevated Beacon,
and refuses to gate the process hosting the BOF. Because it executes inline, it intentionally does
not reproduce the standalone binary's one-minute multi-cycle scheduler.

## Validation workflow

A useful red-team result needs independent evidence. Do not treat a successful filter-add call as
proof that the target experienced the intended brownout.

1. Select a benign canary application whose network behavior is observable.
2. Record an ungated baseline.
3. Run DutchOven with an unrelated control application in parallel.
4. Measure both new connection attempts and an already-established connection.
5. Capture client timestamps, server timestamps, and packets independently.
6. Confirm the control remained unaffected and every pass interval recovered.
7. Confirm no DutchOven filters remain after normal exit and forced termination.

## Operational boundaries

DutchOven intentionally does **not**:

- enumerate installed security products;
- infer executable paths or process ownership;
- stop, suspend, or modify services;
- install a service, driver, persistence mechanism, or boot-time filter;
- change Windows Firewall profiles, Group Policy, or policy-QoS registry state;
- impersonate a commercial agent protocol; or
- claim vendor-side telemetry results without independent backend evidence.

Administrator rights are required because adding WFP objects requires write access to the Base
Filtering Engine. Application identity must match the executable that actually owns the network
flow. Multiprocess agents, proxies, kernel networking, and long-lived connections can change the
observed result and must be measured rather than assumed.

## Repository map

| Path | Contents |
| --- | --- |
| `src/main.c` | Minimal command-line entry point |
| `src/gate.c` | WFP gate, profiles, timing, and cleanup |
| `tests/test_gate.c` | Parser, profile, and boundary tests |
| `bof/` | x64 BOF source, Aggressor wrapper, and contract checker |
