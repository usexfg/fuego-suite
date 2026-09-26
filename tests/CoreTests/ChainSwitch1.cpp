// Copyright (c) 2011-2016 The Cryptonote developers
// Copyright (c) 2014-2016 SDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ChainSwitch1.h"

using namespace CryptoNote;


gen_chain_switch_1::gen_chain_switch_1()
{
  REGISTER_CALLBACK("check_split_not_switched", gen_chain_switch_1::check_split_not_switched);
  REGISTER_CALLBACK("mark_refused_switch", gen_chain_switch_1::mark_refused_switch);
  REGISTER_CALLBACK("check_switch_refused", gen_chain_switch_1::check_switch_refused);
  REGISTER_CALLBACK("check_split_switched", gen_chain_switch_1::check_split_switched);
}


//-----------------------------------------------------------------------------------------------------
bool gen_chain_switch_1::generate(std::vector<test_event_entry>& events) const
{
  uint64_t ts_start = 1338224400;
  /*
  (0 )-(1 )-(2 ) -(3 )-(4 )                       <- main chain, until 8 is connected
              \ |-(5 )-(6 )-(7 )-(8 )|            <- alt chain, until 8 is connected

  Fuego refuses to switch to a heavier alternative chain that lacks any
  transaction of the main-chain segment it would replace (anti double-spend
  reorg rule in Blockchain::switch_to_alternative_blockchain). Upstream this
  scenario switched at (7) and returned the 7-coin tx to the pool; here (7)
  is heavier but lacks that tx, so the switch is refused, and (8) carries it,
  so the switch then succeeds.

  transactions ([n] - tx amount, (m) - block):
  (1)     : miner -[ 5]-> account_1 ( +5 in main chain,  +5 in alt chain)
  (3), (8): miner -[ 7]-> account_2 ( +7 in main chain,  +7 in alt chain), missing from the alt chain until (8)
  (4), (6): miner -[11]-> account_3 (+11 in main chain, +11 in alt chain)
  (5)     : miner -[13]-> account_4 ( +0 in main chain, +13 in alt chain), tx will be in tx pool before switch

  transactions orders ([n] - tx amount, (m) - block):
  miner -[1], [2]-> account_1: in main chain (3), (3), in alt chain (5), (6)
  miner -[1], [2]-> account_2: in main chain (3), (4), in alt chain (5), (5)
  miner -[1], [2]-> account_3: in main chain (3), (4), in alt chain (6), (5)
  miner -[1], [2]-> account_4: in main chain (4), (3), in alt chain (5), (6)
  */

  GENERATE_ACCOUNT(miner_account);

  //                                                                                              events
  MAKE_GENESIS_BLOCK(events, blk_0, miner_account, ts_start);                                     //  0
  MAKE_ACCOUNT(events, recipient_account_1);                                                      //  1
  MAKE_ACCOUNT(events, recipient_account_2);                                                      //  2
  MAKE_ACCOUNT(events, recipient_account_3);                                                      //  3
  MAKE_ACCOUNT(events, recipient_account_4);                                                      //  4
  REWIND_BLOCKS(events, blk_0r, blk_0, miner_account)                                             // <N blocks>
  MAKE_TX(events, tx_00, miner_account, recipient_account_1, MK_COINS(5), blk_0);                 //  5 + N
  MAKE_NEXT_BLOCK_TX1(events, blk_1, blk_0r, miner_account, tx_00);                               //  6 + N
  MAKE_NEXT_BLOCK(events, blk_2, blk_1, miner_account);                                           //  7 + N
  REWIND_BLOCKS(events, blk_2r, blk_2, miner_account)                                             // <N blocks>

  // Transactions to test account balances after switch
  MAKE_TX_LIST_START(events, txs_blk_3, miner_account, recipient_account_2, MK_COINS(7), blk_2);  //  8 + 2N
  MAKE_TX_LIST_START(events, txs_blk_4, miner_account, recipient_account_3, MK_COINS(11), blk_2); //  9 + 2N
  MAKE_TX_LIST_START(events, txs_blk_5, miner_account, recipient_account_4, MK_COINS(13), blk_2); // 10 + 2N
  std::list<Transaction> txs_blk_6;
  txs_blk_6.push_back(txs_blk_4.front());

  // Transactions, that has different order in alt block chains
  MAKE_TX_LIST(events, txs_blk_3, miner_account, recipient_account_1, MK_COINS(1), blk_2);        // 11 + 2N
  txs_blk_5.push_back(txs_blk_3.back());
  MAKE_TX_LIST(events, txs_blk_3, miner_account, recipient_account_1, MK_COINS(2), blk_2);        // 12 + 2N
  txs_blk_6.push_back(txs_blk_3.back());

  MAKE_TX_LIST(events, txs_blk_3, miner_account, recipient_account_2, MK_COINS(1), blk_2);        // 13 + 2N
  txs_blk_5.push_back(txs_blk_3.back());
  MAKE_TX_LIST(events, txs_blk_4, miner_account, recipient_account_2, MK_COINS(2), blk_2);        // 14 + 2N
  txs_blk_5.push_back(txs_blk_4.back());

  MAKE_TX_LIST(events, txs_blk_3, miner_account, recipient_account_3, MK_COINS(1), blk_2);        // 15 + 2N
  txs_blk_6.push_back(txs_blk_3.back());
  MAKE_TX_LIST(events, txs_blk_4, miner_account, recipient_account_3, MK_COINS(2), blk_2);        // 16 + 2N
  txs_blk_5.push_back(txs_blk_4.back());

  MAKE_TX_LIST(events, txs_blk_4, miner_account, recipient_account_4, MK_COINS(1), blk_2);        // 17 + 2N
  txs_blk_5.push_back(txs_blk_4.back());
  MAKE_TX_LIST(events, txs_blk_3, miner_account, recipient_account_4, MK_COINS(2), blk_2);        // 18 + 2N
  txs_blk_6.push_back(txs_blk_3.back());

  MAKE_NEXT_BLOCK_TX_LIST(events, blk_3, blk_2r, miner_account, txs_blk_3);                       // 19 + 2N
  MAKE_NEXT_BLOCK_TX_LIST(events, blk_4, blk_3, miner_account, txs_blk_4);                        // 20 + 2N
  //split
  MAKE_NEXT_BLOCK_TX_LIST(events, blk_5, blk_2r, miner_account, txs_blk_5);                       // 22 + 2N
  MAKE_NEXT_BLOCK_TX_LIST(events, blk_6, blk_5, miner_account, txs_blk_6);                        // 23 + 2N
  DO_CALLBACK(events, "check_split_not_switched");                                                // 23 + 2N
  DO_CALLBACK(events, "mark_refused_switch");                                                     // 24 + 2N
  MAKE_NEXT_BLOCK(events, blk_7, blk_6, miner_account);                                           // 25 + 2N
  DO_CALLBACK(events, "check_switch_refused");                                                    // 26 + 2N
  MAKE_NEXT_BLOCK_TX1(events, blk_8, blk_7, miner_account, txs_blk_3.front());                    // 27 + 2N
  DO_CALLBACK(events, "check_split_switched");                                                    // 28 + 2N

  return true;
}

bool gen_chain_switch_1::check_block_verification_context(const CryptoNote::block_verification_context& bvc, size_t event_idx, const CryptoNote::Block& /*blk*/)
{
  if (event_idx == m_refused_switch_block_idx) {
    return bvc.m_verification_failed && !bvc.m_added_to_main_chain;
  }

  return !bvc.m_verification_failed;
}

bool gen_chain_switch_1::mark_refused_switch(CryptoNote::core& /*c*/, size_t ev_index, const std::vector<test_event_entry>& /*events*/)
{
  m_refused_switch_block_idx = ev_index + 1;
  return true;
}

bool gen_chain_switch_1::check_switch_refused(CryptoNote::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  DEFINE_TESTS_ERROR_CONTEXT("gen_chain_switch_1::check_switch_refused");

  std::list<Block> blocks;
  bool r = c.get_blocks(0, 10000, blocks);
  CHECK_TEST_CONDITION(r);
  CHECK_EQ(5 + 2 * m_currency.minedMoneyUnlockWindow(), blocks.size());
  CHECK_TEST_CONDITION(std::equal(blocks.begin(), blocks.end(), m_chain_1.begin()));

  // (7) outweighs (4) but lacks the 7-coin tx: kept as an alternative block only.
  CHECK_EQ(3, c.get_alternative_blocks_count());

  std::vector<Transaction> tx_pool = c.getPoolTransactions();
  CHECK_EQ(1, tx_pool.size());
  CHECK_TEST_CONDITION(tx_pool.front() == m_tx_pool.front());

  return true;
}


//-----------------------------------------------------------------------------------------------------
bool gen_chain_switch_1::check_split_not_switched(CryptoNote::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  DEFINE_TESTS_ERROR_CONTEXT("gen_chain_switch_1::check_split_not_switched");

  m_recipient_account_1 = boost::get<AccountBase>(events[1]);
  m_recipient_account_2 = boost::get<AccountBase>(events[2]);
  m_recipient_account_3 = boost::get<AccountBase>(events[3]);
  m_recipient_account_4 = boost::get<AccountBase>(events[4]);

  std::list<Block> blocks;
  bool r = c.get_blocks(0, 10000, blocks);
  CHECK_TEST_CONDITION(r);
  CHECK_EQ(5 + 2 * m_currency.minedMoneyUnlockWindow(), blocks.size());
  CHECK_TEST_CONDITION(blocks.back() == boost::get<Block>(events[20 + 2 * m_currency.minedMoneyUnlockWindow()]));  // blk_4

  CHECK_EQ(2, c.get_alternative_blocks_count());

  std::vector<CryptoNote::Block> chain;
  map_hash2tx_t mtx;
  r = find_block_chain(events, chain, mtx, get_block_hash(blocks.back()));
  CHECK_TEST_CONDITION(r);
  CHECK_EQ(MK_COINS(8),  get_balance(m_recipient_account_1, chain, mtx));
  CHECK_EQ(MK_COINS(10), get_balance(m_recipient_account_2, chain, mtx));
  CHECK_EQ(MK_COINS(14), get_balance(m_recipient_account_3, chain, mtx));
  CHECK_EQ(MK_COINS(3),  get_balance(m_recipient_account_4, chain, mtx));

  std::vector<Transaction> tx_pool = c.getPoolTransactions();
  CHECK_EQ(1, tx_pool.size());

  std::vector<size_t> tx_outs;
  uint64_t transfered;
  lookup_acc_outs(m_recipient_account_4.getAccountKeys(), tx_pool.front(), getTransactionPublicKeyFromExtra(tx_pool.front().extra), tx_outs, transfered);
  CHECK_EQ(MK_COINS(13), transfered);

  m_chain_1.swap(blocks);
  m_tx_pool.swap(tx_pool);

  return true;
}

//-----------------------------------------------------------------------------------------------------
bool gen_chain_switch_1::check_split_switched(CryptoNote::core& c, size_t ev_index, const std::vector<test_event_entry>& events)
{
  DEFINE_TESTS_ERROR_CONTEXT("gen_chain_switch_1::check_split_switched");

  std::list<Block> blocks;
  bool r = c.get_blocks(0, 10000, blocks);
  CHECK_TEST_CONDITION(r);
  CHECK_EQ(7 + 2 * m_currency.minedMoneyUnlockWindow(), blocks.size());
  auto it = blocks.end();
  --it; --it; --it; --it;
  CHECK_TEST_CONDITION(std::equal(blocks.begin(), it, m_chain_1.begin()));
  CHECK_TEST_CONDITION(blocks.back() == boost::get<Block>(events[27 + 2 * m_currency.minedMoneyUnlockWindow()]));  // blk_8

  std::list<Block> alt_blocks;
  r = c.get_alternative_blocks(alt_blocks);
  CHECK_TEST_CONDITION(r);
  CHECK_EQ(2, c.get_alternative_blocks_count());

  // Some blocks that were in main chain are in alt chain now
  BOOST_FOREACH(Block b, alt_blocks)
  {
    CHECK_TEST_CONDITION(m_chain_1.end() != std::find(m_chain_1.begin(), m_chain_1.end(), b));
  }

  std::vector<CryptoNote::Block> chain;
  map_hash2tx_t mtx;
  r = find_block_chain(events, chain, mtx, get_block_hash(blocks.back()));
  CHECK_TEST_CONDITION(r);
  CHECK_EQ(MK_COINS(8),  get_balance(m_recipient_account_1, chain, mtx));
  CHECK_EQ(MK_COINS(10), get_balance(m_recipient_account_2, chain, mtx));
  CHECK_EQ(MK_COINS(14), get_balance(m_recipient_account_3, chain, mtx));
  CHECK_EQ(MK_COINS(16), get_balance(m_recipient_account_4, chain, mtx));

  // Every main-chain tx is in the new chain and the 13-coin tx was mined in (5).
  std::vector<Transaction> tx_pool = c.getPoolTransactions();
  CHECK_EQ(0, tx_pool.size());

  return true;
}
