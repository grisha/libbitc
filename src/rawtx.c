/* Copyright 2012 exMULTI, Inc.
 * Distributed under the MIT/X11 software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 */
#include "libbitc-config.h"             // for PACKAGE_VERSION

#include <bitc/base58.h>                // for base58_decode_check
#include <bitc/buffer.h>                // for const_buffer
#include <bitc/buint.h>                 // for bu256_copy, bu256_hex, etc
#include <bitc/clist.h>                 // for clist, clist_append
#include <bitc/core.h>                  // for bitc_tx, bitc_txin, etc
#include <bitc/cstr.h>                  // for cstring, cstr_free, etc
#include <bitc/hexcode.h>               // for encode_hex, hex2str, etc
#include <bitc/parr.h>                  // for parr, parr_add, parr_new, etc
#include <bitc/script.h>                // for bsp_make_pubkeyhash, etc
#include <bitc/util.h>                  // for VALSTR_SZ, btc_decimal

#include <cJSON.h>                      // for cJSON, etc

#include <argp.h>                       // for ARGP_ERR_UNKNOWN, error_t, etc
#include <assert.h>                     // for assert
#include <ctype.h>                      // for isdigit
#include <stdbool.h>                    // for true, bool, false
#include <stddef.h>                     // for size_t
#include <stdint.h>                     // for uint32_t, uint64_t
#include <stdio.h>                      // for fprintf, stderr, printf, etc
#include <stdlib.h>                     // for NULL, exit, atoi, calloc, etc
#include <string.h>                     // for strchr, strcpy, strlen, etc

const char *argp_program_version = PACKAGE_VERSION;

static struct argp_option options[] = {
	{ "blank", 1003, NULL, 0,
	  "Start new, blank transaction. Do not read any input data." },

	{ "locktime", 1001, "LOCKTIME", 0,
	  "Set transaction lock time" },
	{ "nversion", 1002, "VERSION", 0,
	  "Set transaction version" },

	{ "txin", 1010, "TXID:VOUT", 0,
	  "Append a transaction input" },
	{ "delete-txin", 1011, "INDEX", 0,
	  "Delete transaction input at INDEX" },

	{ "txout", 1020, "ADDRESS:AMOUNT-IN-SATOSHIS", 0,
	  "Append a transaction output" },
	{ "delete-txout", 1021, "INDEX", 0,
	  "Delete transaction output at INDEX" },

	{ "output-json", 1030, NULL, 0,
	  "Output transaction as decoded JSON" },

	{ "strict-free", 1040, NULL, 0,
	  "Free memory on exit (default off; only for valgrind)" },

	{ }
};

static struct bitc_tx tx;

static char *opt_locktime;
static char *opt_version;
static char *opt_hexdata;
static bool opt_blank;
static bool opt_decode_json;
static bool opt_strict_free;
static clist *opt_txin;
static clist *opt_del_txin;
static clist *opt_txout;
static clist *opt_del_txout;

static const char doc[] =
"txmod - command line interface to modify bitcoin transactions";
static const char args_doc[] =
"HEX-ENCODED-TX";

static error_t parse_opt (int key, char *arg, struct argp_state *state);

static const struct argp argp = { options, parse_opt, args_doc, doc };

static inline int ptr_to_int(void *p)
{
	unsigned long l = (unsigned long) p;
	return (int) l;
}

static inline void *int_to_ptr(int v)
{
	return (void *)(unsigned long) v;
}

static bool is_digitstr(const char *s)
{
	if (!*s)
		return false;
	while (*s) {
		if (!isdigit(*s))
			return false;
		s++;
	}

	return true;
}

static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
	switch(key) {

	case 1001:			// --locktime=NNNNN
		opt_locktime = arg;
		break;
	case 1002:			// --nversion=NNNNN
		opt_version = arg;
		break;
	case 1003:			// --blank
		opt_blank = true;
		break;
	case 1030:			// --output-json
		opt_decode_json = true;
		break;
	case 1040:			// --strict-free
		opt_strict_free = true;
		break;

	case 1010: {			// --txin=TXID:VOUT
		char *colon = strchr(arg, ':');
		if (!colon)
			return ARGP_ERR_UNKNOWN;
		if ((colon - arg) != 64)
			return ARGP_ERR_UNKNOWN;
		if (!is_digitstr(colon + 1))
			return ARGP_ERR_UNKNOWN;

		char hexstr[65];
		memcpy(hexstr, arg, 64);
		hexstr[64] = 0;
		if (!is_hexstr(hexstr, false))
			return ARGP_ERR_UNKNOWN;

		opt_txin = clist_append(opt_txin, strdup(arg));
		break;
	 }
	case 1011: {			// --delete-txin=INDEX
		if (!is_digitstr(arg))
			return ARGP_ERR_UNKNOWN;

		opt_del_txin = clist_append(opt_del_txin,
					     int_to_ptr(atoi(arg)));
		break;
	 }

	case 1020: {			// --txout=ADDRESS:AMOUNT
		char *colon = strchr(arg, ':');
		if (!colon)
			return ARGP_ERR_UNKNOWN;
		unsigned int partlen = colon - arg;
		if (!is_digitstr(colon + 1))
			return ARGP_ERR_UNKNOWN;

		char addrstr[partlen + 1];
		memcpy(addrstr, arg, partlen);
		addrstr[partlen] = 0;

		cstring *payload = base58_decode_check(NULL, addrstr);
		bool payload_invalid = (!payload || (payload->len != 21));
		cstr_free(payload, true);
		if (payload_invalid)
			return ARGP_ERR_UNKNOWN;

		opt_txout = clist_append(opt_txout, strdup(arg));
		break;
	 }
	case 1021: {			// --delete-txout=INDEX
		if (!is_digitstr(arg))
			return ARGP_ERR_UNKNOWN;

		opt_del_txout = clist_append(opt_del_txout,
					      int_to_ptr(atoi(arg)));
		break;
	 }


	case ARGP_KEY_ARG:
		if (opt_hexdata)
			return ARGP_ERR_UNKNOWN;
		opt_hexdata = arg;
		break;

	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static void mutate_locktime(void)
{
	long long ll = strtoll(opt_locktime, NULL, 10);
	if ((ll != 0LL) && (ll < 1395466455LL || ll > 0xffffffffLL)) {
		fprintf(stderr, "invalid lock time %lld\n", ll);
		exit(1);
	}

	tx.nLockTime = (uint32_t) ll;
}

static void mutate_version(void)
{
	int nVersion = atoi(opt_version);
	if (nVersion < 1 || nVersion > 3) {
		fprintf(stderr, "invalid tx version %d\n", nVersion);
		exit(1);
	}

	tx.nVersion = (uint32_t) nVersion;
}

static void append_input(char *txid_str, char *vout_str)
{
	bu256_t txid;
	if (!hex_bu256(&txid, txid_str)) {
		fprintf(stderr, "invalid txid hex\n");
		exit(1);
	}

	unsigned int vout = atoi(vout_str);

	struct bitc_txin *txin = calloc(1, sizeof(struct bitc_txin));
	if (!txin) {
		fprintf(stderr, "OOM\n");
		exit(1);
	}
	bitc_txin_init(txin);

	bu256_copy(&txin->prevout.hash, &txid);
	txin->prevout.n = vout;
	txin->scriptSig = cstr_new(NULL);
	txin->nSequence = SEQUENCE_FINAL;

	parr_add(tx.vin, txin);
}

static void mutate_inputs(void)
{
	if (!tx.vin)
		tx.vin = parr_new(8, bitc_txin_freep);

	// delete inputs
	clist *tmp = opt_del_txin;
	while (tmp) {
		int idx = ptr_to_int(tmp->data);
		tmp = tmp->next;

		if ((idx >= 0) && (idx < tx.vin->len))
			parr_remove_idx(tx.vin, idx);
	}

	// append new inputs
	tmp = opt_txin;
	while (tmp) {
		char *arg = tmp->data;
		tmp = tmp->next;

		size_t alloc_len = strlen(arg) + 1;
		char txid_str[alloc_len];
		strcpy(txid_str, arg);

		char *colon = strchr(txid_str, ':');
		*colon = 0;

		char vout_str[alloc_len];
		strcpy(vout_str, colon + 1);

		append_input(txid_str, vout_str);
	}
}

static bool is_script_addr(unsigned char addrtype)
{
	switch (addrtype) {
	case SCRIPT_ADDRESS:		// mainnet
	case SCRIPT_ADDRESS_TEST:	// testnet
		return true;
	default:
		return false;
	}
}

static void append_output(char *addr_str, char *amount_str)
{
	unsigned char addrtype = 0;
	cstring *payload = base58_decode_check(&addrtype, addr_str);
	bool is_script = is_script_addr(addrtype);

	uint64_t amt = (uint64_t) strtoull(amount_str, NULL, 10);

	struct bitc_txout *txout = calloc(1, sizeof(struct bitc_txout));
	if (!txout || !payload) {
		fprintf(stderr, "OOM\n");
		exit(1);
	}

	txout->nValue = amt;

	if (is_script)
		txout->scriptPubKey = bsp_make_scripthash(payload);
	else
		txout->scriptPubKey = bsp_make_pubkeyhash(payload);

	parr_add(tx.vout, txout);
}

static void mutate_outputs(void)
{
	if (!tx.vout)
		tx.vout = parr_new(8, bitc_txout_freep);

	// delete outputs
	clist *tmp = opt_del_txout;
	while (tmp) {
		int idx = ptr_to_int(tmp->data);
		tmp = tmp->next;

		if ((idx >= 0) && (idx < tx.vin->len))
			parr_remove_idx(tx.vout, idx);
	}

	// append new outputs
	tmp = opt_txout;
	while (tmp) {
		char *arg = tmp->data;
		tmp = tmp->next;

		size_t alloc_len = strlen(arg) + 1;
		char addr_str[alloc_len];
		strcpy(addr_str, arg);

		char *colon = strchr(addr_str, ':');
		*colon = 0;

		char amount_str[alloc_len];
		strcpy(amount_str, colon + 1);

		append_output(addr_str, amount_str);
	}
}

static void apply_mutations(void)
{
	if (opt_locktime)
		mutate_locktime();
	if (opt_version)
		mutate_version();
	if (opt_txin || opt_del_txin)
		mutate_inputs();
	if (opt_txout || opt_del_txout)
		mutate_outputs();
}

static void read_data(void)
{
	if (!opt_hexdata) {
		fprintf(stderr, "no input data\n");
		exit(1);
	}

	cstring *txbuf = hex2str(opt_hexdata);
	if (!txbuf) {
		fprintf(stderr, "invalid input data\n");
		exit(1);
	}
	struct const_buffer cbuf = { txbuf->str, txbuf->len };

	if (!deser_bitc_tx(&tx, &cbuf)) {
		fprintf(stderr, "TX decode failed\n");
		exit(1);
	}

	cstr_free(txbuf, true);
}

static void bitc_json_set_new_int(cJSON *obj, const char *key, int i)
{
	cJSON_AddNumberToObject(obj, key, i);
}

static void bitc_json_set_new_int256(cJSON *obj, const char *key, const bu256_t *i)
{
	char hexstr[(32 * 2) + 1];
	bu256_hex(hexstr, i);

	cJSON_AddStringToObject(obj, key, hexstr);
}

static void bitc_json_set_new_script(cJSON *obj_parent, const char *key,
				   cstring *s)
{
	cJSON *obj = cJSON_CreateObject();
	assert(obj != NULL);

	cJSON_AddItemToObject(obj_parent, key, obj);

	char raw_hexstr[(s->len * 2) + 1];
	encode_hex(raw_hexstr, s->str, s->len);

	cJSON *hexstr = cJSON_CreateString(raw_hexstr);
	assert(hexstr != NULL);

	cJSON_AddItemToObject(obj, "hex", hexstr);
}

static void output_json_txid(cJSON *obj)
{
	bitc_tx_calc_sha256(&tx);

	bitc_json_set_new_int256(obj, "txid", &tx.sha256);
}

static void output_json_txin(cJSON *arr, const struct bitc_txin *txin)
{
	cJSON *obj = cJSON_CreateObject();
	assert(obj != NULL);

	bitc_json_set_new_int256(obj, "txid", &txin->prevout.hash);
	bitc_json_set_new_int(obj, "vout", txin->prevout.n);
	bitc_json_set_new_script(obj, "scriptSig", txin->scriptSig);
	bitc_json_set_new_int(obj, "sequence", txin->nSequence);

	cJSON_AddItemToArray(arr, obj);;
}

static void output_json_vin(cJSON *obj)
{
	cJSON *arr = cJSON_CreateArray();
	assert(arr != NULL);

	cJSON_AddItemToObject(obj, "vin", arr);

	if (!tx.vin)
		return;

	unsigned int i;
	for (i = 0; i < tx.vin->len; i++) {
		struct bitc_txin *txin = parr_idx(tx.vin, i);
		output_json_txin(arr, txin);
	}
}

static void output_json_txout(cJSON *arr, const struct bitc_txout *txout,
				unsigned int n_idx)
{
	cJSON *obj = cJSON_CreateObject();
	assert(obj != NULL);

	char raw_valstr[VALSTR_SZ];
	btc_decimal(raw_valstr, VALSTR_SZ, txout->nValue);

	double raw_val = strtod(raw_valstr, NULL);
	cJSON_AddNumberToObject(obj, "value_FIXME", raw_val);

	bitc_json_set_new_int(obj, "n", n_idx);
	bitc_json_set_new_script(obj, "scriptPubKey", txout->scriptPubKey);

	cJSON_AddItemToArray(arr, obj);
}

static void output_json_vout(cJSON *obj)
{
	cJSON *arr = cJSON_CreateArray();
	assert(arr != NULL);

	cJSON_AddItemToObject(obj, "vout", arr);

	if (!tx.vout)
		return;

	unsigned int i;
	for (i = 0; i < tx.vout->len; i++) {
		struct bitc_txout *txout = parr_idx(tx.vout, i);
		output_json_txout(arr, txout, i);
	}
}

static void output_data_json(void)
{
	cJSON *obj = cJSON_CreateObject();
	assert(obj != NULL);

	output_json_txid(obj);
	bitc_json_set_new_int(obj, "version", tx.nVersion);
	bitc_json_set_new_int(obj, "locktime", tx.nLockTime);
	output_json_vin(obj);
	output_json_vout(obj);

	fprintf(stdout, "%s\n", cJSON_Print(obj));

	if (opt_strict_free)
		cJSON_Delete(obj);
}

static void output_data_hex(void)
{
	size_t alloc_len = opt_hexdata ? strlen(opt_hexdata) : 512;
	cstring *s = cstr_new_sz(alloc_len);
	ser_bitc_tx(s, &tx);

	char hexstr[(s->len * 2) + 1];
	encode_hex(hexstr, s->str, s->len);
	printf("%s\n", hexstr);

	cstr_free(s, true);
}

static void output_data(void)
{
	if (opt_decode_json)
		output_data_json();
	else
		output_data_hex();
}

int main (int argc, char *argv[])
{
	error_t aprc;

	aprc = argp_parse(&argp, argc, argv, 0, NULL, NULL);
	if (aprc) {
		fprintf(stderr, "argp_parse failed: %s\n", strerror(aprc));
		return 1;
	}

	bitc_tx_init(&tx);

	if (!opt_blank)
		read_data();

	apply_mutations();

	output_data();

	if (opt_strict_free)
		bitc_tx_free(&tx);

	return 0;
}

