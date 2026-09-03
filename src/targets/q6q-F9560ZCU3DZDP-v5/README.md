# q6q-F9560ZCU3DZDP v5 rebaseline

Isolated rebaseline candidate derived from the q6q v4 target header.

- keeps the firmware-derived offsets, direct-source P0 table, and inverse runtime mapping;
- keeps the q6q SLUB/object bounds and physical P0 gates;
- disables the MTE-aware KernelSnitch matcher for an A/B comparison because v4 failed before the P0 gate on this device;
- has not been run on hardware and must not be treated as usable.
