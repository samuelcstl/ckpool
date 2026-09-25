/* BIP310 protocol arithmetic regression tests. */

#include "config.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bip310.h"

int main(void)
{
	uint32_t mask;
	char formatted[9];

	assert(bip310_parse_mask("1fffe000", &mask));
	assert(mask == 0x1fffe000U);
	assert(bip310_parse_mask("0000E000", &mask));
	assert(mask == 0x0000e000U);
	assert(!bip310_parse_mask("e000", &mask));
	assert(!bip310_parse_mask("0000x000", &mask));
	assert(!bip310_parse_mask(NULL, &mask));

	bip310_format_mask(formatted, 0x0000e000U);
	assert(!strcmp(formatted, "0000e000"));

	/* LCC: an ASIC may offer the full BIP320 mask while the pool exposes
	 * only the three chain-safe version bits. */
	mask = bip310_negotiate_mask(0x0000e000U, 0x1fffe000U);
	assert(mask == 0x0000e000U);
	assert(bip310_popcount(mask) == 3);

	/* Miner may only touch bits in the last negotiated mask. */
	assert(bip310_version_bits_valid(0x00006000U, mask));
	assert(!bip310_version_bits_valid(0x00010000U, mask));

	/* Replacement is the critical BIP310 rule. A rolled zero MUST clear a
	 * job-version bit inside the negotiated mask; OR semantics would fail. */
	assert(bip310_apply_version(0x2000e006U, 0x0000e000U, 0x00002000U)
	       == 0x20002006U);
	assert(bip310_apply_version(0x2000e006U, 0x0000e000U, 0x00000000U)
	       == 0x20000006U);

	/* Bits outside the negotiated mask always remain owned by the job. */
	assert(bip310_apply_version(0x20400006U, 0x0000e000U, 0x00006000U)
	       == 0x20406006U);

	puts("BIP310 tests passed");
	return 0;
}
