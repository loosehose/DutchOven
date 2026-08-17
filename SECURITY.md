# Security and authorized use

DutchOven is an adversarial security-control validation project. Use it only in a controlled environment or on systems and tenants for which you have explicit authorization.

The default red-core build enforces these boundaries:

- The standalone binary exposes only the bounded application gate.
- Targets must be explicit, local, absolute executable paths; product discovery is not implemented.
- At most 16 targets and 15 minutes of runtime are accepted.
- WFP objects are created in a dynamic engine session and are not marked persistent or boot-time.
- IPv4 and IPv6 filters change atomically, and normal exit or Ctrl+C removes active filters.
- No service state, driver state, firewall profile, Group Policy, or policy-QoS registry state is changed.
- The BOF accepts one explicit path, limits a pulse to 0.5–5 seconds, refuses to gate its hosting
  process, and closes its dynamic WFP session before returning.

Do not submit features whose primary purpose is broad deployment, persistence, credential access, destructive interference, or evasion without measurement.
