// Copyright (c) 2019 The BCD Core developers

#ifndef BITCOINDIAMOND_NET_BLOCKTX_H
#define BITCOINDIAMOND_NET_BLOCKTX_H

#include <sync.h>
#include <primitives/transaction.h>
#include <primitives/block.h>
#include <blockencodings.h>
#include <chain.h>
#include <consensus/validation.h>
#include "net.h"
#include <network/net_cnode_state.h>

struct COrphanTx {
    // When modifying, adapt the copy of this definition in tests/DoS_tests.
    CTransactionRef tx;
    NodeId fromPeer;
    int64_t nTimeExpire;
};

struct IteratorComparator
{
    template<typename I>
    bool operator()(const I& a, const I& b) const
    {
        return &(*a) < &(*b);
    }
};
/** When our tip was last updated. */
std::atomic<int64_t> g_last_tip_update(0);

/**
  * Sources of received blocks, saved to be able to send them reject
  * messages or ban them when processing happens afterwards.
  * Set mapBlockSource[hash].second to false if the node should not be
  * punished if the block is invalid.
  */
std::map<uint256, std::pair<NodeId, bool>> mapBlockSource GUARDED_BY(cs_main);


/** How frequently to check for extra outbound peers and disconnect, in seconds */
static constexpr int64_t EXTRA_PEER_CHECK_INTERVAL = 45;
/// Age after which a stale block will no longer be served if requested as
/// protection against fingerprinting. Set to one month, denominated in seconds.
static constexpr int STALE_RELAY_AGE_LIMIT = 30 * 24 * 60 * 60;
/// Age after which a block is considered historical for purposes of rate
/// limiting block relay. Set to one week, denominated in seconds.
static constexpr int HISTORICAL_BLOCK_AGE = 7 * 24 * 60 * 60;
/** Expiration time for orphan transactions in seconds */
static constexpr int64_t ORPHAN_TX_EXPIRE_TIME = 20 * 60;
/** Minimum time between orphan transactions expire time checks in seconds */
static constexpr int64_t ORPHAN_TX_EXPIRE_INTERVAL = 5 * 60;

/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** Default number of orphan+recently-replaced txn to keep around for block reconstruction */
static const unsigned int DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN = 100;


/** Relay map */
typedef std::map<uint256, CTransactionRef> MapRelay;
MapRelay mapRelay GUARDED_BY(cs_main);
/** Expiration-time ordered list of (expire time, relay map entry) pairs. */
std::deque<std::pair<int64_t, MapRelay::iterator>> vRelayExpiration GUARDED_BY(cs_main);

std::atomic<int64_t> nTimeBestReceived(0); // Used only to inform the wallet of when we last received a block
/** Number of peers from which we're downloading blocks. */
int nPeersWithValidatedDownloads GUARDED_BY(cs_main) = 0;

// All of the following cache a recent block, and are protected by cs_most_recent_block
static CCriticalSection cs_most_recent_block;
static std::shared_ptr<const CBlock> most_recent_block GUARDED_BY(cs_most_recent_block);
static std::shared_ptr<const CBlockHeaderAndShortTxIDs> most_recent_compact_block GUARDED_BY(cs_most_recent_block);
static uint256 most_recent_block_hash GUARDED_BY(cs_most_recent_block);
static bool fWitnessesPresentInMostRecentCompactBlock GUARDED_BY(cs_most_recent_block);


CCriticalSection g_cs_orphans;
std::map<uint256, COrphanTx> mapOrphanTransactions GUARDED_BY(g_cs_orphans);

std::map<COutPoint, std::set<std::map<uint256, COrphanTx>::iterator, IteratorComparator>> mapOrphanTransactionsByPrev GUARDED_BY(g_cs_orphans);

static size_t vExtraTxnForCompactIt GUARDED_BY(g_cs_orphans) = 0;
static std::vector<std::pair<uint256, CTransactionRef>> vExtraTxnForCompact GUARDED_BY(g_cs_orphans);

std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> > mapBlocksInFlight GUARDED_BY(cs_main);

/** Stack of nodes which we have set to announce using compact blocks */
std::list<NodeId> lNodesAnnouncingHeaderAndIDs GUARDED_BY(cs_main);

class NetBlockTx {

private:
    CConnman* const connman;

public:
    explicit NetBlockTx(CConnman* connmanIn);
    void EraseOrphansFor(NodeId peer);

    void AddToCompactExtraTransactions(const CTransactionRef &tx) EXCLUSIVE_LOCKS_REQUIRED(g_cs_orphans);

    bool AddOrphanTx(const CTransactionRef &tx, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(g_cs_orphans);

    int EraseOrphanTx(uint256 hash) EXCLUSIVE_LOCKS_REQUIRED(g_cs_orphans);

    unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);

    bool BlockRequestAllowed(const CBlockIndex *pindex, const Consensus::Params &consensusParams);

    void BlockConnected(const std::shared_ptr<const CBlock> &pblock, const CBlockIndex *pindex,
                        const std::vector<CTransactionRef> &vtxConflicted);

    void NewPoWValidBlock(const CBlockIndex *pindex, const std::shared_ptr<const CBlock> &pblock);

    void UpdatedBlockTip(const CBlockIndex *pindexNew, const CBlockIndex *pindexFork, bool fInitialDownload);

    void BlockChecked(const CBlock &block, const CValidationState &state);

    void ProcessGetBlockData(CNode *pfrom, const CChainParams &chainparams, const CInv &inv, CConnman *connman);

    void ProcessGetData(CNode *pfrom, const CChainParams &chainparams, CConnman *connman,
                        const std::atomic<bool> &interruptMsgProc);

    bool TipMayBeStale(const Consensus::Params &consensusParams) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void UpdateBlockAvailability(NodeId nodeid, const uint256 &hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void ProcessBlockAvailability(NodeId nodeid) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, const CBlockIndex* pindex = nullptr, std::list<QueuedBlock>::iterator** pit = nullptr) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    bool MarkBlockAsReceived(const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void MaybeSetPeerAsAnnouncingHeaderAndIDs(NodeId nodeid, CConnman* connman);
    bool PeerHasHeader(CNodeState *state, const CBlockIndex *pindex) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
    void FindNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<const CBlockIndex*>& vBlocks, NodeId& nodeStaller, const Consensus::Params& consensusParams) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
};

#endif //BITCOINDIAMOND_NET_BLOCKTX_H