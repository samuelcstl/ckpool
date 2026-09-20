#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "coinbase_extension.h"

static yyjson_doc *parse(const char *json)
{
	return yyjson_read(json, strlen(json), 0);
}

int main(void)
{
	coinbase_extension_plan_t plan;
	unsigned char out[128];
	yyjson_doc *doc;
	yyjson_val *root;
	size_t len;

	assert(coinbase_extension_type_valid("5trat-delayed-jackpot"));
	assert(!coinbase_extension_type_valid("5trat"));
	assert(!coinbase_extension_type_valid(NULL));

	doc = parse("{\"5trat\":{\"previous_bonus\":0,\"previous_payout_script\":\"\"}}");
	assert(doc);
	root = yyjson_doc_get_root(doc);
	assert(coinbase_extension_plan("5trat-delayed-jackpot", root, 475000000, &plan));
	assert(plan.miner_value == 475000000);
	assert(plan.output_count == 0);
	yyjson_doc_free(doc);

	doc = parse("{\"5trat\":{\"previous_bonus\":50000000,\"previous_payout_script\":\"76a91400112233445566778899aabbccddeeff0011223388ac\"}}");
	assert(doc);
	root = yyjson_doc_get_root(doc);
	assert(coinbase_extension_plan("5trat-delayed-jackpot", root, 525000000, &plan));
	assert(plan.miner_value == 475000000);
	assert(plan.output_count == 1);
	assert(plan.outputs[0].value == 50000000);
	assert(plan.outputs[0].script_len == 25);
	len = coinbase_extension_serialize_output(out, sizeof(out), &plan.outputs[0]);
	assert(len == 34);
	assert(out[8] == 25);
	assert(!memcmp(out + 9, plan.outputs[0].script, 25));
	yyjson_doc_free(doc);

	doc = parse("{\"5trat\":{\"previous_bonus\":200000000,\"previous_payout_script\":\"76a91400112233445566778899aabbccddeeff0011223388ac\"}}");
	assert(doc);
	root = yyjson_doc_get_root(doc);
	assert(coinbase_extension_plan("5trat-delayed-jackpot", root, 675000000, &plan));
	assert(plan.miner_value == 475000000);
	assert(plan.outputs[0].value == 200000000);
	yyjson_doc_free(doc);

	doc = parse("{\"5trat\":{\"previous_bonus\":50000000,\"previous_payout_script\":\"\"}}");
	assert(doc);
	assert(!coinbase_extension_plan("5trat-delayed-jackpot", yyjson_doc_get_root(doc),
					525000000, &plan));
	yyjson_doc_free(doc);

	doc = parse("{\"5trat\":{\"previous_bonus\":0,\"previous_payout_script\":\"00\"}}");
	assert(doc);
	assert(!coinbase_extension_plan("5trat-delayed-jackpot", yyjson_doc_get_root(doc),
					475000000, &plan));
	yyjson_doc_free(doc);

	doc = parse("{\"5trat\":{\"previous_bonus\":700000000,\"previous_payout_script\":\"00\"}}");
	assert(doc);
	assert(!coinbase_extension_plan("5trat-delayed-jackpot", yyjson_doc_get_root(doc),
					675000000, &plan));
	yyjson_doc_free(doc);

	doc = parse("{}");
	assert(doc);
	assert(!coinbase_extension_plan("5trat-delayed-jackpot", yyjson_doc_get_root(doc),
					475000000, &plan));
	yyjson_doc_free(doc);

	return 0;
}
