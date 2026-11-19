# Learn_10 F248 native skip — standard integration

The native third-cinematic skip is now compiled in the **ordinary**
Windows x86 `Rayman2Fix.exe`. No separate research build, environment
variable or debug logging is needed. The separate `SkipLearn10` INI toggle
is explicitly OFF by default.

## User configuration

```ini
[Features]
SkipCinematics=1
; Set SkipLearn10 to 1 to enable the guarded F248 skip
SkipLearn10=0

[Debug]
Log=0
```

`SkipCinematics=1` keeps the original `jail_20` input skip enabled by
default. The guarded native `learn_10` F248 skip additionally requires
`SkipLearn10=1`. Its default `0` leaves the third cinematic unchanged.
`SkipCinematics=0` also blocks F248 even if `SkipLearn10=1`.
`Log=0` or `Log=1` does not affect either skip.

## Native conditions preserved

The game calls original F248 (`GetBooleanInArray`) first. Only its
**temporary result**, for the identified director, native behaviour 3,
exact root `IfNot(F248(Fix.V42, 1188))`, first visit, and original
integer-0 result, can be changed from 0 to 1. No story progression bit
or shared AI script node is directly written. The original input and
one-second timing guard remain native.

Function-table replacement is guarded by a verified original pointer,
native code-byte signature, writable memory and compare/exchange.
The hook is disarmed at director exit, native handover, observed timeout,
or level exit. Other F248 calls are forwarded to the native function.

## Validation

PR #34 passed Linux and Windows x86 CI and was user-validated on
Wine-staging 11.19, including visual transition, camera and controls.
This PR compiles the feature into standard packaging while leaving it
inactive until users enable `SkipLearn10=1`. It tests both INI switches
and verifies
that `Log=0` no longer prevents director identification.

Normal Windows gameplay was not separately tested at the user's
request. Engine-visible exceptional teardown paths remain beyond
what a compile/test workflow can prove. This is **not a release** until
the user approves a merge or release.
