
#include "libbitc-config.h"

#include <bitc/script.h>
#include <bitc/key.h>
#include <bitc/core.h>
#include <bitc/buint.h>
#include <bitc/key.h>
#include <bitc/util.h>

static bool sign1(const bu160_t *key_id, struct bitc_keystore *ks,
		  const bu256_t *hash, int nHashType,
		  cstring *scriptSig)
{
	struct bitc_key key;
	bool rc = false;

	bitc_key_init(&key);

	/* find private key in keystore */
	if (!bkeys_key_get(ks, key_id, &key))
		goto out;

	void *sig = NULL;
	size_t siglen = 0;

	/* sign hash with private key */
	if (!bitc_sign(&key, hash, sizeof(*hash), &sig, &siglen))
		goto out;

	/* append nHashType to signature */
	unsigned char ch = (unsigned char) nHashType;
	sig = realloc(sig, siglen + 1);
	memcpy(sig + siglen, &ch, 1);
	siglen++;

	/* append signature to scriptSig */
	bsp_push_data(scriptSig, sig, siglen);
	free(sig);

	rc = true;

out:
	bitc_key_free(&key);
	return rc;
}

bool bitc_script_sign(struct bitc_keystore *ks, const cstring *fromPubKey,
		    const struct bitc_tx *txTo, unsigned int nIn,
		    int nHashType)
{
	if (!txTo || !txTo->vin || nIn >= txTo->vin->len)
		return false;

	struct bitc_txin *txin = parr_idx(txTo->vin, nIn);

	/* get signature hash */
	bu256_t hash;
        bitc_tx_sighash(&hash, fromPubKey, txTo, nIn, nHashType, 0, 0);

        /* match fromPubKey against templates, to find what pubkey[hashes]
	 * are required for signing
	 */
	struct bscript_addr addrs;
	if (!bsp_addr_parse(&addrs, fromPubKey->str, fromPubKey->len))
		return false;

	cstring *scriptSig = cstr_new_sz(64);
	bool rc = false;
	bu160_t key_id;
	struct buffer *kbuf;

	/* sign, based on script template matched above */
	switch (addrs.txtype) {
	case TX_PUBKEY:
		kbuf = addrs.pub->data;
		bu_Hash160((unsigned char *)&key_id, kbuf->p, kbuf->len);

		if (!sign1(&key_id, ks, &hash, nHashType, scriptSig))
			goto out;
		break;

	case TX_PUBKEYHASH:
		kbuf = addrs.pubhash->data;
		memcpy(&key_id, kbuf->p, kbuf->len);

		if (!sign1(&key_id, ks, &hash, nHashType, scriptSig))
			goto out;
		if (!bkeys_pubkey_append(ks, &key_id, scriptSig))
			goto out;
		break;

	case TX_SCRIPTHASH:		/* TODO; not supported yet */
	case TX_MULTISIG:
		goto out;

	case TX_NONSTANDARD:		/* unknown script type, cannot sign */
		goto out;
	}

	if (txin->scriptSig)
		cstr_free(txin->scriptSig, true);
	txin->scriptSig = scriptSig;
	scriptSig = NULL;
	rc = true;

out:
	if (scriptSig)
		cstr_free(scriptSig, true);
	bsp_addr_free(&addrs);
	return rc;
}

bool bitc_sign_sig(struct bitc_keystore *ks, const struct bitc_utxo *txFrom,
		 struct bitc_tx *txTo, unsigned int nIn,
		 unsigned int flags, int nHashType)
{
	if (!ks || !txFrom || !txFrom->vout ||
	    !txTo || !txTo->vin || nIn >= txTo->vin->len)
		return false;

	struct bitc_txin *txin = parr_idx(txTo->vin, nIn);

	if (txin->prevout.n >= txFrom->vout->len)
		return false;
	struct bitc_txout *txout = parr_idx(txFrom->vout,
						   txin->prevout.n);

	return bitc_script_sign(ks, txout->scriptPubKey, txTo, nIn, nHashType);
}

