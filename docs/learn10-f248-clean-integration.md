# Learn_10 F248 native skip — standard integration

The native third-cinematic skip is now compiled in the **ordinary**
Windows x86 `Rayman2Fix.exe`. No separate research build, environment
variable, debug logging or new INI option is needed.

## User configuration

```ini
[Features]
SkipCinematics=1

[Debug]
Log=0
```

`SkipCinematics=1` enables both the original `jail_20` input skip and
the guarded, native `learn_10` F248 skip. With `SkipCinematics=0`,
the F248 function pointer is **not installed** and the native cinematic
runs unmodified. `Log=0` or `Log=1` does not affect either skip.

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
This PR switches on the feature in standard packaging; it includes
tests for `SkipCinematics=0` and `SkipCinematics=1`, and verifies
that `Log=0` no longer prevents director identification.

Normal Windows gameplay was not separately tested at the user's
request. Engine-visible exceptional teardown paths remain beyond
what a compile/test workflow can prove. This is **not a release** until
the user approves a merge or release.
