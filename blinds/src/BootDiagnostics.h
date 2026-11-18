#pragma once

namespace BootDiagnostics {
void printResetReason();
// Run once before starting peripherals; halt startup if this returns false.
bool checkRam();
[[noreturn]] void halt();
}
