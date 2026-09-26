// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2014-2016 SDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <boost/serialization/vector.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/variant.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/map.hpp>
#include <boost/foreach.hpp>
#include <boost/serialization/is_bitwise_serializable.hpp>
#include "CryptoNoteCore/CryptoNoteBasic.h"
#include "UnorderedContainersBoostSerialization.h"
#include "crypto/crypto.h"

//namespace CryptoNote {
namespace boost
{
  namespace serialization
  {

  //---------------------------------------------------
  template <class Archive>
  inline void serialize(Archive &a, Crypto::PublicKey &x, const boost::serialization::version_type ver)
  {
    a & reinterpret_cast<char (&)[sizeof(Crypto::PublicKey)]>(x);
  }
  template <class Archive>
  inline void serialize(Archive &a, Crypto::SecretKey &x, const boost::serialization::version_type ver)
  {
    a & reinterpret_cast<char (&)[sizeof(Crypto::SecretKey)]>(x);
  }
  template <class Archive>
  inline void serialize(Archive &a, Crypto::KeyDerivation &x, const boost::serialization::version_type ver)
  {
    a & reinterpret_cast<char (&)[sizeof(Crypto::KeyDerivation)]>(x);
  }
  template <class Archive>
  inline void serialize(Archive &a, Crypto::KeyImage &x, const boost::serialization::version_type ver)
  {
    a & reinterpret_cast<char (&)[sizeof(Crypto::KeyImage)]>(x);
  }

  template <class Archive>
  inline void serialize(Archive &a, Crypto::Signature &x, const boost::serialization::version_type ver)
  {
    a & reinterpret_cast<char (&)[sizeof(Crypto::Signature)]>(x);
  }
  template <class Archive>
  inline void serialize(Archive &a, Crypto::Hash &x, const boost::serialization::version_type ver)
  {
    a & reinterpret_cast<char (&)[sizeof(Crypto::Hash)]>(x);
  }
  
  template <class Archive> void serialize(Archive& archive, CryptoNote::MultisignatureInput &output, unsigned int version) {
    archive & output.amount;
    archive & output.signatureCount;
    archive & output.outputIndex;
  }

  template <class Archive> void serialize(Archive& archive, CryptoNote::MultisignatureOutput &output, unsigned int version) {
    archive & output.keys;
    archive & output.requiredSignatureCount;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::KeyOutput &x, const boost::serialization::version_type ver)
  {
    a & x.key;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::BaseInput &x, const boost::serialization::version_type ver)
  {
    a & x.blockIndex;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::KeyInput &x, const boost::serialization::version_type ver)
  {
    a & x.amount;
    a & x.outputIndexes;
    a & x.keyImage;
  }

  // ── Input/output variants added after this harness was written ──
  // The chaingen event log is serialized through boost; without these, the
  // TransactionInput/TransactionOutput variants cannot be archived at all.
  // EllipticCurvePoint/EllipticCurveScalar need their own entries: in C++,
  // Crypto::PublicKey/SecretKey DERIVE from them (the typedefs in CryptoTypes.h
  // apply to C only), so the PublicKey/SecretKey serializers above do not match
  // a field declared as the base type. No ambiguity results — an exact
  // PublicKey& match outranks the derived-to-base conversion.
  template <class Archive>
  inline void serialize(Archive &a, Crypto::EllipticCurvePoint &x, const boost::serialization::version_type ver)
  {
    a & reinterpret_cast<char (&)[sizeof(Crypto::EllipticCurvePoint)]>(x);
  }

  template <class Archive>
  inline void serialize(Archive &a, Crypto::EllipticCurveScalar &x, const boost::serialization::version_type ver)
  {
    a & reinterpret_cast<char (&)[sizeof(Crypto::EllipticCurveScalar)]>(x);
  }

  template <class Archive>
  inline void serialize(Archive &a, Crypto::MembershipProof &x, const boost::serialization::version_type ver)
  {
    a & reinterpret_cast<char (&)[sizeof(Crypto::MembershipProof)]>(x);
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::TransactionInputCommitmentSpend &x, const boost::serialization::version_type ver)
  {
    a & x.amount;
    a & x.outputIndexes;
    a & x.keyImage;
    a & x.claimedInterest;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::TransactionInputCommitmentTransfer &x, const boost::serialization::version_type ver)
  {
    a & x.amount;
    a & x.outputIndexes;
    a & x.keyImage;
    a & x.newTerm;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::TransactionInputSwapEscrow &x, const boost::serialization::version_type ver)
  {
    a & x.amount;
    a & x.escrowTxId;
    a & x.escrowOutputIndex;
    a & x.mode;
    a & x.keyImage;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::TransactionInputUnified &x, const boost::serialization::version_type ver)
  {
    a & x.outputIndexes;
    a & x.keyImage;
    a & x.pseudoCommitment;
    a & x.sigC0;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::TransactionOutputCommitment &x, const boost::serialization::version_type ver)
  {
    a & x.commitKey;
    a & x.term;
    a & x.amountCommitment;
    a & x.amountProof;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::TransactionOutputOrder &x, const boost::serialization::version_type ver)
  {
    a & x.side;
    a & x.price;
    a & x.expiration;
    a & x.spendKey;
    a & x.viewKey;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::TransactionOutputSwapEscrow &x, const boost::serialization::version_type ver)
  {
    a & x.claimKey;
    a & x.refundKey;
    a & x.adaptorPoint;
    a & x.refundTimeout;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::TransactionOutputUnified &x, const boost::serialization::version_type ver)
  {
    a & x.key;
    a & x.term;
    a & x.commitment;
    a & x.proof;
  }

  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::TransactionOutput &x, const boost::serialization::version_type ver)
  {
    a & x.amount;
    a & x.target;
  }


  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::Transaction &x, const boost::serialization::version_type ver)
  {
    a & x.version;
    a & x.unlockTime;
    a & x.inputs;
    a & x.outputs;
    a & x.extra;
    a & x.signatures;
  }


  template <class Archive>
  inline void serialize(Archive &a, CryptoNote::Block &b, const boost::serialization::version_type ver)
  {
    a & b.majorVersion;
    a & b.minorVersion;
    a & b.timestamp;
    a & b.previousBlockHash;
    a & b.nonce;
    //------------------
    a & b.baseTransaction;
    a & b.transactionHashes;
  }
}
}

//}
