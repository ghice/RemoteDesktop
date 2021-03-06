#ifndef CRYPTOPP_FHMQV_H
#define CRYPTOPP_FHMQV_H

#include "cryptopp/gfpcrypt.h"
#include "cryptopp/algebra.h"
#include "cryptopp/sha.h"
#include "cryptopp/eccrypto.h"
#include <assert.h>

NAMESPACE_BEGIN(CryptoPP)

template <class GROUP_PARAMETERS, class COFACTOR_OPTION = CPP_TYPENAME GROUP_PARAMETERS::DefaultCofactorOption, class HASH = SHA256>
class FHMQV_Domain : public AuthenticatedKeyAgreementDomain
{
public:
	typedef GROUP_PARAMETERS GroupParameters;
	typedef typename GroupParameters::Element Element;
	typedef FHMQV_Domain<GROUP_PARAMETERS, COFACTOR_OPTION, HASH> Domain;

	FHMQV_Domain(bool clientRole = true) : m_role(clientRole ? RoleClient : RoleServer) { }

	FHMQV_Domain(const GroupParameters &params, bool clientRole = true)
		: m_role(clientRole ? RoleClient : RoleServer), m_groupParameters(params) {}

	FHMQV_Domain(BufferedTransformation &bt, bool clientRole = true)
		: m_role(clientRole ? RoleClient : RoleServer)
	{
		m_groupParameters.BERDecode(bt);
	}

	template <class T1>
	FHMQV_Domain(T1 v1, bool clientRole = true)
		: m_role(clientRole ? RoleClient : RoleServer)
	{
		m_groupParameters.Initialize(v1);
	}

	template <class T1, class T2>
	FHMQV_Domain(T1 v1, T2 v2, bool clientRole = true)
		: m_role(clientRole ? RoleClient : RoleServer)
	{
		m_groupParameters.Initialize(v1, v2);
	}

	template <class T1, class T2, class T3>
	FHMQV_Domain(T1 v1, T2 v2, T3 v3, bool clientRole = true)
		: m_role(clientRole ? RoleClient : RoleServer)
	{
		m_groupParameters.Initialize(v1, v2, v3);
	}

	template <class T1, class T2, class T3, class T4>
	FHMQV_Domain(T1 v1, T2 v2, T3 v3, T4 v4, bool clientRole = true)
		: m_role(clientRole ? RoleClient : RoleServer)
	{
		m_groupParameters.Initialize(v1, v2, v3, v4);
	}

protected:

	inline void Hash(const Element* sigma,
		const byte* e1, size_t e1len, const byte* e2, size_t e2len,
		const byte* s1, size_t s1len, const byte* s2, size_t s2len,
		byte* digest, size_t dlen) const
	{
		assert(dlen <= (size_t)HASH::DIGESTSIZE);

		HASH hash;

		if (sigma)
		{
			const DL_GroupParameters<Element> &params = GetAbstractGroupParameters();
			SecByteBlock sbb(params.GetEncodedElementSize(true));
			params.EncodeElement(true, *sigma, sbb.BytePtr());
			hash.Update(sbb.BytePtr(), sbb.SizeInBytes());
		}

		hash.Update(e1, e1len);
		hash.Update(e2, e2len);
		hash.Update(s1, s1len);
		hash.Update(s2, s2len);

		const size_t sz = std::min(dlen, (size_t)HASH::DIGESTSIZE);
		hash.TruncatedFinal(digest, sz);
	}

public:

	const GroupParameters & GetGroupParameters() const { return m_groupParameters; }
	GroupParameters & AccessGroupParameters(){ return m_groupParameters; }

	CryptoParameters & AccessCryptoParameters(){ return AccessAbstractGroupParameters(); }

	//! return length of agreed value produced
	unsigned int AgreedValueLength() const { return (unsigned int)HASH::DIGESTSIZE; }
	//! return length of static private keys in this domain
	unsigned int StaticPrivateKeyLength() const { return GetAbstractGroupParameters().GetSubgroupOrder().ByteCount(); }
	//! return length of static public keys in this domain
	unsigned int StaticPublicKeyLength() const{ return GetAbstractGroupParameters().GetEncodedElementSize(true); }

	//! generate static private key
	/*! \pre size of privateKey == PrivateStaticKeyLength() */
	void GenerateStaticPrivateKey(RandomNumberGenerator &rng, byte *privateKey) const
	{
		Integer x(rng, Integer::One(), GetAbstractGroupParameters().GetMaxExponent());
		x.Encode(privateKey, StaticPrivateKeyLength());
	}

	//! generate static public key
	/*! \pre size of publicKey == PublicStaticKeyLength() */
	void GenerateStaticPublicKey(RandomNumberGenerator &rng, const byte *privateKey, byte *publicKey) const
	{
		const DL_GroupParameters<Element> &params = GetAbstractGroupParameters();
		Integer x(privateKey, StaticPrivateKeyLength());
		Element y = params.ExponentiateBase(x);
		params.EncodeElement(true, y, publicKey);
	}

	unsigned int EphemeralPrivateKeyLength() const { return StaticPrivateKeyLength() + StaticPublicKeyLength(); }
	unsigned int EphemeralPublicKeyLength() const{ return StaticPublicKeyLength(); }

	//! return length of ephemeral private keys in this domain
	void GenerateEphemeralPrivateKey(RandomNumberGenerator &rng, byte *privateKey) const
	{
		const DL_GroupParameters<Element> &params = GetAbstractGroupParameters();
		Integer x(rng, Integer::One(), params.GetMaxExponent());
		x.Encode(privateKey, StaticPrivateKeyLength());
		Element y = params.ExponentiateBase(x);
		params.EncodeElement(true, y, privateKey + StaticPrivateKeyLength());
	}

	//! return length of ephemeral public keys in this domain
	void GenerateEphemeralPublicKey(RandomNumberGenerator &rng, const byte *privateKey, byte *publicKey) const
	{
		memcpy(publicKey, privateKey + StaticPrivateKeyLength(), EphemeralPublicKeyLength());
	}

	//! derive agreed value from your private keys and couterparty's public keys, return false in case of failure
	/*! \note The ephemeral public key will always be validated.
	If you have previously validated the static public key, use validateStaticOtherPublicKey=false to save time.
	\pre size of agreedValue == AgreedValueLength()
	\pre length of staticPrivateKey == StaticPrivateKeyLength()
	\pre length of ephemeralPrivateKey == EphemeralPrivateKeyLength()
	\pre length of staticOtherPublicKey == StaticPublicKeyLength()
	\pre length of ephemeralOtherPublicKey == EphemeralPublicKeyLength()
	*/
	bool Agree(byte *agreedValue,
		const byte *staticPrivateKey, const byte *ephemeralPrivateKey,
		const byte *staticOtherPublicKey, const byte *ephemeralOtherPublicKey,
		bool validateStaticOtherPublicKey = true) const
	{
		byte *XX = NULL, *YY = NULL, *AA = NULL, *BB = NULL;
		size_t xxs = 0, yys = 0, aas = 0, bbs = 0;

		// Depending on the role, this will hold either A's or B's static
		// (long term) public key. AA or BB will then point into tt.
		SecByteBlock tt(StaticPublicKeyLength());

		try
		{
			const DL_GroupParameters<Element> &params = GetAbstractGroupParameters();

			if (m_role == RoleServer)
			{
				Integer b(staticPrivateKey, StaticPrivateKeyLength());
				Element B = params.ExponentiateBase(b);
				params.EncodeElement(true, B, tt);

				XX = const_cast<byte*>(ephemeralOtherPublicKey);
				xxs = EphemeralPublicKeyLength();
				YY = const_cast<byte*>(ephemeralPrivateKey)+StaticPrivateKeyLength();
				yys = EphemeralPublicKeyLength();
				AA = const_cast<byte*>(staticOtherPublicKey);
				aas = StaticPublicKeyLength();
				BB = tt.BytePtr();
				bbs = tt.SizeInBytes();
			}
			else if (m_role == RoleClient)
			{
				Integer a(staticPrivateKey, StaticPrivateKeyLength());
				Element A = params.ExponentiateBase(a);
				params.EncodeElement(true, A, tt);

				XX = const_cast<byte*>(ephemeralPrivateKey)+StaticPrivateKeyLength();
				xxs = EphemeralPublicKeyLength();
				YY = const_cast<byte*>(ephemeralOtherPublicKey);
				yys = EphemeralPublicKeyLength();
				AA = tt.BytePtr();
				aas = tt.SizeInBytes();
				BB = const_cast<byte*>(staticOtherPublicKey);
				bbs = StaticPublicKeyLength();
			}
			else
			{
				assert(0);
				return false;
			}

			// DecodeElement calls ValidateElement at level 1. Level 1 only calls
			// VerifyPoint to ensure the element is in G*. If the other's PublicKey is
			// requested to be validated, we manually call ValidateElement at level 3.
			Element VV1 = params.DecodeElement(staticOtherPublicKey, false);
			if (!params.ValidateElement(validateStaticOtherPublicKey ? 3 : 1, VV1, NULL))
				return false;

			// DecodeElement calls ValidateElement at level 1. Level 1 only calls
			// VerifyPoint to ensure the element is in G*. Crank it up.
			Element VV2 = params.DecodeElement(ephemeralOtherPublicKey, false);
			if (!params.ValidateElement(3, VV2, NULL))
				return false;

			const Integer& p = params.GetGroupOrder();
			const Integer& q = params.GetSubgroupOrder();
			const unsigned int len /*bytes*/ = (((q.BitCount() + 1) / 2 + 7) / 8);

			Integer d, e;
			SecByteBlock dd(len), ee(len);

			Hash(NULL, XX, xxs, YY, yys, AA, aas, BB, bbs, dd.BytePtr(), dd.SizeInBytes());
			d.Decode(dd.BytePtr(), dd.SizeInBytes());

			Hash(NULL, YY, yys, XX, xxs, AA, aas, BB, bbs, ee.BytePtr(), ee.SizeInBytes());
			e.Decode(ee.BytePtr(), ee.SizeInBytes());

			Element sigma;
			if (m_role == RoleServer)
			{
				Integer y(ephemeralPrivateKey, StaticPrivateKeyLength());
				Integer b(staticPrivateKey, StaticPrivateKeyLength());
				Integer s_B = (y + e * b) % q;

				Element A = params.DecodeElement(AA, false);
				Element X = params.DecodeElement(XX, false);

				Element t1 = params.ExponentiateElement(A, d);
				Element t2 = m_groupParameters.MultiplyElements(X, t1);

				sigma = params.ExponentiateElement(t2, s_B);
			}
			else
			{
				Integer x(ephemeralPrivateKey, StaticPrivateKeyLength());
				Integer a(staticPrivateKey, StaticPrivateKeyLength());
				Integer s_A = (x + d * a) % q;

				Element B = params.DecodeElement(BB, false);
				Element Y = params.DecodeElement(YY, false);

				Element t1 = params.ExponentiateElement(B, e);
				Element t2 = m_groupParameters.MultiplyElements(Y, t1);

				sigma = params.ExponentiateElement(t2, s_A);
			}

			Hash(&sigma, XX, xxs, YY, yys, AA, aas, BB, bbs, agreedValue, AgreedValueLength());
		}
		catch (DL_BadElement &)
		{
			return false;
		}
		return true;
	}

private:

	// The paper uses Initiator and Recipient - make it classical.
	enum KeyAgreementRole{ RoleServer = 1, RoleClient };

	DL_GroupParameters<Element> & AccessAbstractGroupParameters() { return m_groupParameters; }
	const DL_GroupParameters<Element> & GetAbstractGroupParameters() const{ return m_groupParameters; }

	KeyAgreementRole m_role;
	GroupParameters m_groupParameters;
};

typedef FHMQV_Domain<DL_GroupParameters_GFP_DefaultSafePrime> FullyHashedMQV;

template <class EC, class COFACTOR_OPTION = CPP_TYPENAME DL_GroupParameters_EC<EC>::DefaultCofactorOption, class HASH = SHA256>
struct FHMQV
{
	typedef FHMQV_Domain<DL_GroupParameters_EC<EC>, COFACTOR_OPTION, HASH> Domain;
};

NAMESPACE_END

#endif


