# Data store boundary

Future bounded caches for telemetry, debug series, firmware progress, and
business logs belong here. Sampling and GUI refresh rates must remain
independent. No global singleton or high-rate GUI signal is introduced in the
current framework.
