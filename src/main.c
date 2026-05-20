/*
 * FRUA — Atari Falcon030 / TT030 port.
 *
 * Bootstrap entry point. Currently a toolchain smoke test: if this builds
 * into a .prg and prints on a Falcon/TT (or in Hatari), the cross toolchain
 * and build system are wired up correctly.
 *
 * The real port will replace this with: platform HAL init -> Mac Toolbox
 * shim init -> decompiled FRUA entry point.
 */

#include <mint/osbind.h>

int main(void)
{
	Cconws("Forgotten Realms: Unlimited Adventures\r\n");
	Cconws("Falcon030 / TT030 port -- toolchain smoke test.\r\n");
	Cconws("Press any key to exit.\r\n");
	Cconin();
	return 0;
}
