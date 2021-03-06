/* Copyright 2012 exMULTI, Inc.
 * Distributed under the MIT/X11 software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 */
#include "libbitc-config.h"

#include <bitc/buint.h>                 // for bu256_t, bu256_new, etc
#include <bitc/core.h>                  // for bitc_block, bitc_tx, etc
#include <bitc/coredefs.h>              // for ::MAX_BLOCK_WEIGHT, etc
#include <bitc/cstr.h>                  // for cstring
#include <bitc/parr.h>                  // for parr, parr_idx, parr_add, etc
#include <bitc/serialize.h>             // for u256_from_compact
#include <bitc/util.h>                  // for bu_Hash_, MIN

#include <gmp.h>                        // for mpz_clear, mpz_init, mpz_t, etc

#include <stdbool.h>                    // for false, bool, true
#include <stdint.h>                     // for int64_t
#include <string.h>                     // for NULL, memset
#include <time.h>                       // for time, time_t

static bool bitc_has_dup_inputs(const struct bitc_tx *tx)
{
	if (!tx->vin || !tx->vin->len || tx->vin->len == 1)
		return false;

	struct bitc_txin *txin, *txin_tmp;
	unsigned int i, j;
	for (i = 0; i < tx->vin->len; i++) {
		txin = parr_idx(tx->vin, i);
		for (j = 0; j < tx->vin->len; j++) {
			if (i == j)
				continue;
			txin_tmp = parr_idx(tx->vin, j);

			if (bitc_outpt_equal(&txin->prevout,
					   &txin_tmp->prevout))
				return true;
		}
	}

	return false;
}

bool bitc_tx_valid(const struct bitc_tx *tx)
{
	unsigned int i;

	// Basic checks
	if (!tx->vin || !tx->vin->len)
		return false;
	if (!tx->vout || !tx->vout->len)
		return false;

	// Size limits
	if (bitc_tx_ser_size(tx) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
		return false;

	// Check for negative or overflow output values
	int64_t value_total = 0;
	for (i = 0; i < tx->vout->len; i++) {
		struct bitc_txout *txout;

		txout = parr_idx(tx->vout, i);
		if (!bitc_txout_valid(txout))
			return false;

		value_total += txout->nValue;
	}

	if (!bitc_valid_value(value_total))
		return false;

	// Check for duplicate inputs
	if (bitc_has_dup_inputs(tx))
		return false;

	if (bitc_tx_coinbase(tx)) {
		struct bitc_txin *txin = parr_idx(tx->vin, 0);

		if (txin->scriptSig->len < 2 ||
		    txin->scriptSig->len > 100)
			return false;
	} else {
		for (i = 0; i < tx->vin->len; i++) {
			struct bitc_txin *txin;

			txin = parr_idx(tx->vin, i);
			if (bitc_outpt_null(&txin->prevout))
				return false;
		}
	}

	return true;
}

parr *bitc_block_merkle_tree(const struct bitc_block *block)
{
	if (!block->vtx || !block->vtx->len)
		return NULL;

	parr *arr = parr_new(0, bu256_freep);

	unsigned int i;
	for (i = 0; i < block->vtx->len; i++) {
		struct bitc_tx *tx;

		tx = parr_idx(block->vtx, i);
		bitc_tx_calc_sha256(tx);

		parr_add(arr, bu256_new(&tx->sha256));
	}

	unsigned int j = 0, nSize;
	for (nSize = block->vtx->len; nSize > 1; nSize = (nSize + 1) / 2) {
		for (i = 0; i < nSize; i += 2) {
			unsigned int i2 = MIN(i+1, nSize-1);
			bu256_t hash;
			bu_Hash_((unsigned char *) &hash,
			   parr_idx(arr, j+i), sizeof(bu256_t),
			   parr_idx(arr, j+i2),sizeof(bu256_t));

			parr_add(arr, bu256_new(&hash));
		}

		j += nSize;
	}

	return arr;
}

void bitc_block_merkle(bu256_t *vo, const struct bitc_block *block)
{
	memset(vo, 0, sizeof(*vo));

	if (!block->vtx || !block->vtx->len)
		return;

	parr *arr = bitc_block_merkle_tree(block);
	if (!arr)
		return;

	bu256_copy(vo, parr_idx(arr, arr->len - 1));

	parr_free(arr, true);
}

parr *bitc_block_merkle_branch(const struct bitc_block *block,
			       const parr *mrktree,
			       unsigned int txidx)
{
	if (!block || !block->vtx || !mrktree || (txidx >= block->vtx->len))
		return NULL;

	parr *ret = parr_new(0, bu256_freep);

	unsigned int j = 0, nSize;
	for (nSize = block->vtx->len; nSize > 1; nSize = (nSize + 1) / 2) {
		unsigned int i = MIN(txidx ^ 1, nSize - 1);
		parr_add(ret, bu256_new(parr_idx(mrktree, j+i)));
		txidx >>= 1;
		j += nSize;
	}

	return ret;
}

void bitc_check_merkle_branch(bu256_t *hash, const bu256_t *txhash_in,
			    const parr *mrkbranch, unsigned int txidx)
{
	bu256_copy(hash, txhash_in);

	unsigned int i;
	for (i = 0; i < mrkbranch->len; i++) {
		const bu256_t *otherside = parr_idx(mrkbranch, i);
		if (txidx & 1)
			bu_Hash_((unsigned char *)hash,
				 otherside, sizeof(bu256_t),
				 hash, sizeof(bu256_t));
		else
			bu_Hash_((unsigned char *)hash,
				 hash, sizeof(bu256_t),
				 otherside, sizeof(bu256_t));

		txidx >>= 1;
	}
}

static bool bitc_block_valid_target(struct bitc_block *block)
{
	mpz_t target, sha256;
	mpz_init(target);
	mpz_init(sha256);

	u256_from_compact(target, block->nBits);
	bu256_bn(sha256, &block->sha256);

	int cmp = mpz_cmp(sha256, target);

	mpz_clear(target);
	mpz_clear(sha256);

	if (cmp > 0)			/* sha256 > target */
		return false;

	return true;
}

static bool bitc_block_valid_merkle(struct bitc_block *block)
{
	bu256_t merkle;

	bitc_block_merkle(&merkle, block);

	return bu256_equal(&merkle, &block->hashMerkleRoot);
}

bool bitc_block_valid(struct bitc_block *block)
{
	bitc_block_calc_sha256(block);

	if (!block->vtx || !block->vtx->len)
		return false;

	if (block->vtx->len * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
		return false;

	if (bitc_block_ser_size(block) * WITNESS_SCALE_FACTOR > MAX_BLOCK_WEIGHT)
		return false;

	if (!bitc_block_valid_target(block)) return false;

	time_t now = time(NULL);
	if (block->nTime > (now + (2 * 60 * 60)))
		return false;

	if (!bitc_block_valid_merkle(block)) return false;

	unsigned int i;
	for (i = 0; i < block->vtx->len; i++) {
		struct bitc_tx *tx;

		tx = parr_idx(block->vtx, i);
		if (!bitc_tx_valid(tx))
			return false;

		bool is_coinbase_idx = (i == 0);
		bool is_coinbase = bitc_tx_coinbase(tx);

		if (is_coinbase != is_coinbase_idx)
			return false;
	}

	return true;
}

