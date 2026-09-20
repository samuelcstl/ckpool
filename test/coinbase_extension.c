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

	/* Captured from live 5TRAT GBT height 18746 after a GOLD parent. */
	doc = parse("{\"height\":18746,\"coinbasevalue\":675000000,\"5trat\":{\"base_subsidy\":475000000,\"previous_bonus\":200000000,\"previous_payout_script\":\"76a914fd303cb4d26c821bafcd73a32d63a571b24052df88ac\",\"previous_tier\":\"gold\",\"settlement\":\"next-block\"}}");
	assert(doc);
	root = yyjson_doc_get_root(doc);
	assert(coinbase_extension_plan("5trat-delayed-jackpot", root, 675000000, &plan));
	assert(plan.miner_value == 475000000);
	assert(plan.outputs[0].value == 200000000);
	assert(plan.outputs[0].script_len == 25);
	{
		static const unsigned char gold_script[25] = {
			0x76, 0xa9, 0x14, 0xfd, 0x30, 0x3c, 0xb4, 0xd2, 0x6c,
			0x82, 0x1b, 0xaf, 0xcd, 0x73, 0xa3, 0x2d, 0x63, 0xa5,
			0x71, 0xb2, 0x40, 0x52, 0xdf, 0x88, 0xac
		};
		assert(!memcmp(plan.outputs[0].script, gold_script, sizeof(gold_script)));
	}
	len = coinbase_extension_serialize_output(out, sizeof(out), &plan.outputs[0]);
	assert(len == 34);
	assert(out[0] == 0x00 && out[1] == 0xc2 && out[2] == 0xeb && out[3] == 0x0b);
	assert(out[4] == 0x00 && out[5] == 0x00 && out[6] == 0x00 && out[7] == 0x00);
	assert(out[8] == 25);
	assert(!memcmp(out + 9, gold_script, sizeof(gold_script)));
	yyjson_doc_free(doc);

	doc = parse("{\"5trat\":{\"previous_bonus\":50000000,\"previous_payout_script\":\"\"}}");
	assert(doc);
	assert(!coinbase_extension_plan("5trat-delayed-jackpot", yyjson_doc_get_root(doc),
					525000000, &plan));
	yyjson_doc_free(doc);

	doc = parse("{\"5trat\":{\"previous_bonus\":0,\"previous_payout_script\":\"00\"}}");
	assert(doc);
	assert(coinbase_extension_plan("5trat-delayed-jackpot", yyjson_doc_get_root(doc),
				       475000000, &plan));
	assert(plan.output_count == 0);
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
