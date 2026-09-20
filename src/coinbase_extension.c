#include "coinbase_extension.h"

#include <string.h>

#define FIVETRAT_MAX_SCRIPT_BYTES 75

static int hex_nibble(char value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	return -1;
}

static bool plan_fivetrat_delayed_jackpot(yyjson_val *gbt,
					 uint64_t coinbase_value,
					 coinbase_extension_plan_t *plan)
{
	yyjson_val *ns, *bonus_val, *script_val;
	const char *script;
	int64_t bonus;
	size_t hex_len, i;
	coinbase_extension_output_t *output;

	ns = gbt ? yyjson_obj_get(gbt, "5trat") : NULL;
	if (!ns || !yyjson_is_obj(ns))
		return false;

	bonus_val = yyjson_obj_get(ns, "previous_bonus");
	script_val = yyjson_obj_get(ns, "previous_payout_script");
	if (!bonus_val || !yyjson_is_int(bonus_val) ||
	    !script_val || !yyjson_is_str(script_val))
		return false;

	bonus = yyjson_get_sint(bonus_val);
	script = yyjson_get_str(script_val);
	if (bonus < 0 || !script || (uint64_t)bonus > coinbase_value)
		return false;

	plan->miner_value = coinbase_value;
	if (!bonus)
		return true;

	hex_len = strlen(script);
	if (!hex_len || (hex_len & 1) || hex_len > FIVETRAT_MAX_SCRIPT_BYTES * 2)
		return false;

	output = &plan->outputs[0];
	output->value = (uint64_t)bonus;
	output->script_len = hex_len / 2;
	for (i = 0; i < output->script_len; i++) {
		int high = hex_nibble(script[i * 2]);
		int low = hex_nibble(script[i * 2 + 1]);

		if (high < 0 || low < 0)
			return false;
		output->script[i] = (unsigned char)((high << 4) | low);
	}

	plan->output_count = 1;
	plan->miner_value -= output->value;
	return true;
}

bool coinbase_extension_type_valid(const char *type)
{
	return type && !strcmp(type, "5trat-delayed-jackpot");
}

bool coinbase_extension_plan(const char *type, yyjson_val *gbt,
			     uint64_t coinbase_value,
			     coinbase_extension_plan_t *plan)
{
	if (!plan || !coinbase_extension_type_valid(type))
		return false;

	memset(plan, 0, sizeof(*plan));
	plan->miner_value = coinbase_value;

	if (!strcmp(type, "5trat-delayed-jackpot"))
		return plan_fivetrat_delayed_jackpot(gbt, coinbase_value, plan);

	return false;
}

size_t coinbase_extension_serialize_output(unsigned char *out, size_t capacity,
					   const coinbase_extension_output_t *output)
{
	uint64_t value;
	size_t needed;

	if (!out || !output || !output->script_len ||
	    output->script_len > COINBASE_EXTENSION_MAX_SCRIPT_BYTES)
		return 0;

	needed = 8 + 1 + output->script_len;
	if (capacity < needed)
		return 0;

	value = output->value;
	for (size_t i = 0; i < 8; i++)
		out[i] = (unsigned char)(value >> (i * 8));
	out[8] = (unsigned char)output->script_len;
	memcpy(out + 9, output->script, output->script_len);
	return needed;
}
