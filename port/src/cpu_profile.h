#pragma once
// Linux CPU-time PC sampling, opt-in via NIER_CPU_PROFILE=<private JSON path>.
void NierStartCpuProfile();
void NierStopCpuProfile();
