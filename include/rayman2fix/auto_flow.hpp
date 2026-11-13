#pragma once

namespace r2fix {
class Startup;

// Default launch path: verify existing game assets, or discover a single
// adjacent original CD image and prepare the installation. Never start an
// unverified retail executable or an inert bootstrap runtime.
int run_automatic(Startup& startup, bool install_only = false);

// Explicit diagnostic verification of previously installed CD assets.
int verify_installation(Startup& startup);
}
