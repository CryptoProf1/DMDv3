// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers 
// Copyright (c) 2015-2017 The DMD Diamond developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"

#include "random.h"
#include "util.h"
#include "utilstrencodings.h"

#include <assert.h>

#include <boost/assign/list_of.hpp>



//GN
#include "crypto/scrypt.h"
//#include "util.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <openssl/sha.h>


static const int SCRYPT_SCRATCHPAD_SIZE = 131072 + 63;

void
PBKDF2_SHA256(const uint8_t *passwd, size_t passwdlen, const uint8_t *salt,
    size_t saltlen, uint64_t c, uint8_t *buf, size_t dkLen);

static inline uint32_t le32dec(const void *pp)
{
        const uint8_t *p = (uint8_t const *)pp;
        return ((uint32_t)(p[0]) + ((uint32_t)(p[1]) << 8) +
            ((uint32_t)(p[2]) << 16) + ((uint32_t)(p[3]) << 24));
}

static inline void le32enc(void *pp, uint32_t x)
{
        uint8_t *p = (uint8_t *)pp;
        p[0] = x & 0xff;
        p[1] = (x >> 8) & 0xff;
        p[2] = (x >> 16) & 0xff;
        p[3] = (x >> 24) & 0xff;
}


static inline uint32_t be32dec(const void *pp)
{
        const uint8_t *p = (uint8_t const *)pp;
        return ((uint32_t)(p[3]) + ((uint32_t)(p[2]) << 8) +
            ((uint32_t)(p[1]) << 16) + ((uint32_t)(p[0]) << 24));
}

static inline void be32enc(void *pp, uint32_t x)
{
        uint8_t *p = (uint8_t *)pp;
        p[3] = x & 0xff;
        p[2] = (x >> 8) & 0xff;
        p[1] = (x >> 16) & 0xff;
        p[0] = (x >> 24) & 0xff;
}

typedef struct HMAC_SHA256Context {
        SHA256_CTX ictx;
        SHA256_CTX octx;
} HMAC_SHA256_CTX;

/* Initialize an HMAC-SHA256 operation with the given key. */
static void
HMAC_SHA256_Init(HMAC_SHA256_CTX *ctx, const void *_K, size_t Klen)
{
        unsigned char pad[64];
        unsigned char khash[32];
        const unsigned char *K = (const unsigned char *)_K;
        size_t i;

        /* If Klen > 64, the key is really SHA256(K). */
        if (Klen > 64) {
                SHA256_Init(&ctx->ictx);
                SHA256_Update(&ctx->ictx, K, Klen);
                SHA256_Final(khash, &ctx->ictx);
                K = khash;
                Klen = 32;
        }

        /* Inner SHA256 operation is SHA256(K xor [block of 0x36] || data). */
        SHA256_Init(&ctx->ictx);
        memset(pad, 0x36, 64);
        for (i = 0; i < Klen; i++)
                pad[i] ^= K[i];
        SHA256_Update(&ctx->ictx, pad, 64);

        /* Outer SHA256 operation is SHA256(K xor [block of 0x5c] || hash). */
        SHA256_Init(&ctx->octx);
        memset(pad, 0x5c, 64);
        for (i = 0; i < Klen; i++)
                pad[i] ^= K[i];
        SHA256_Update(&ctx->octx, pad, 64);

        /* Clean the stack. */
        memset(khash, 0, 32);
}

/* Add bytes to the HMAC-SHA256 operation. */
static void
HMAC_SHA256_Update(HMAC_SHA256_CTX *ctx, const void *in, size_t len)
{
        /* Feed data to the inner SHA256 operation. */
        SHA256_Update(&ctx->ictx, in, len);
}

/* Finish an HMAC-SHA256 operation. */
static void
HMAC_SHA256_Final(unsigned char digest[32], HMAC_SHA256_CTX *ctx)
{
        unsigned char ihash[32];

        /* Finish the inner SHA256 operation. */
        SHA256_Final(ihash, &ctx->ictx);

        /* Feed the inner hash to the outer SHA256 operation. */
        SHA256_Update(&ctx->octx, ihash, 32);

        /* Finish the outer SHA256 operation. */
        SHA256_Final(digest, &ctx->octx);

        /* Clean the stack. */
        memset(ihash, 0, 32);
}

/**
 * PBKDF2_SHA256(passwd, passwdlen, salt, saltlen, c, buf, dkLen):
 * Compute PBKDF2(passwd, salt, c, dkLen) using HMAC-SHA256 as the PRF, and
 * write the output to buf.  The value dkLen must be at most 32 * (2^32 - 1).
 */
      ///////////////////////

#define ROTL(a, b) (((a) << (b)) | ((a) >> (32 - (b))))

static inline void xor_salsa8(uint32_t B[16], const uint32_t Bx[16])
{
        uint32_t x00,x01,x02,x03,x04,x05,x06,x07,x08,x09,x10,x11,x12,x13,x14,x15;
        int i;

        x00 = (B[ 0] ^= Bx[ 0]);
        x01 = (B[ 1] ^= Bx[ 1]);
        x02 = (B[ 2] ^= Bx[ 2]);
        x03 = (B[ 3] ^= Bx[ 3]);
        x04 = (B[ 4] ^= Bx[ 4]);
        x05 = (B[ 5] ^= Bx[ 5]);
        x06 = (B[ 6] ^= Bx[ 6]);
        x07 = (B[ 7] ^= Bx[ 7]);
        x08 = (B[ 8] ^= Bx[ 8]);
        x09 = (B[ 9] ^= Bx[ 9]);
        x10 = (B[10] ^= Bx[10]);
        x11 = (B[11] ^= Bx[11]);
        x12 = (B[12] ^= Bx[12]);
        x13 = (B[13] ^= Bx[13]);
        x14 = (B[14] ^= Bx[14]);
        x15 = (B[15] ^= Bx[15]);
        for (i = 0; i < 8; i += 2) {
                /* Operate on columns. */
                x04 ^= ROTL(x00 + x12,  7);  x09 ^= ROTL(x05 + x01,  7);
                x14 ^= ROTL(x10 + x06,  7);  x03 ^= ROTL(x15 + x11,  7);

                x08 ^= ROTL(x04 + x00,  9);  x13 ^= ROTL(x09 + x05,  9);
                x02 ^= ROTL(x14 + x10,  9);  x07 ^= ROTL(x03 + x15,  9);

                x12 ^= ROTL(x08 + x04, 13);  x01 ^= ROTL(x13 + x09, 13);
                x06 ^= ROTL(x02 + x14, 13);  x11 ^= ROTL(x07 + x03, 13);

                x00 ^= ROTL(x12 + x08, 18);  x05 ^= ROTL(x01 + x13, 18);
                x10 ^= ROTL(x06 + x02, 18);  x15 ^= ROTL(x11 + x07, 18);

                /* Operate on rows. */
                x01 ^= ROTL(x00 + x03,  7);  x06 ^= ROTL(x05 + x04,  7);
                x11 ^= ROTL(x10 + x09,  7);  x12 ^= ROTL(x15 + x14,  7);

                x02 ^= ROTL(x01 + x00,  9);  x07 ^= ROTL(x06 + x05,  9);
                x08 ^= ROTL(x11 + x10,  9);  x13 ^= ROTL(x12 + x15,  9);

                x03 ^= ROTL(x02 + x01, 13);  x04 ^= ROTL(x07 + x06, 13);
                x09 ^= ROTL(x08 + x11, 13);  x14 ^= ROTL(x13 + x12, 13);

                x00 ^= ROTL(x03 + x02, 18);  x05 ^= ROTL(x04 + x07, 18);
                x10 ^= ROTL(x09 + x08, 18);  x15 ^= ROTL(x14 + x13, 18);
        }
        B[ 0] += x00;
        B[ 1] += x01;
        B[ 2] += x02;
        B[ 3] += x03;
        B[ 4] += x04;
        B[ 5] += x05;
        B[ 6] += x06;
        B[ 7] += x07;
        B[ 8] += x08;
        B[ 9] += x09;
        B[10] += x10;
        B[11] += x11;
        B[12] += x12;
        B[13] += x13;
        B[14] += x14;
        B[15] += x15;
}

void scrypt_1024_1_1_256_sp_generic(const char *input, char *output, char *scratchpad)
{
        uint8_t B[128];
        uint32_t X[32];
        uint32_t *V;
        uint32_t i, j, k;

        V = (uint32_t *)(((uintptr_t)(scratchpad) + 63) & ~ (uintptr_t)(63));

        PBKDF2_SHA256((const uint8_t *)input, 80, (const uint8_t *)input, 80, 1, B, 128);

        for (k = 0; k < 32; k++)
                X[k] = le32dec(&B[4 * k]);

        for (i = 0; i < 1024; i++) {
                memcpy(&V[i * 32], X, 128);
                xor_salsa8(&X[0], &X[16]);
                xor_salsa8(&X[16], &X[0]);
        }
        for (i = 0; i < 1024; i++) {
                j = 32 * (X[16] & 1023);
                for (k = 0; k < 32; k++)
                        X[k] ^= V[j + k];
                xor_salsa8(&X[0], &X[16]);
                xor_salsa8(&X[16], &X[0]);
        }

        for (k = 0; k < 32; k++)
                le32enc(&B[4 * k], X[k]);

        PBKDF2_SHA256((const uint8_t *)input, 80, B, 128, 1, (uint8_t *)output, 32);
}

#if defined(USE_SSE2)
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_AMD64) || (defined(MAC_OSX) && defined(__i386__))
/* Always SSE2 */
void scrypt_detect_sse2(unsigned int cpuid_edx)
{
    printf("scrypt: using scrypt-sse2 as built.\n");
}
#else
/* Detect SSE2 */
void (*scrypt_1024_1_1_256_sp)(const char *input, char *output, char *scratchpad);

void scrypt_detect_sse2(unsigned int cpuid_edx)
{
    if (cpuid_edx & 1<<26)
    {
        scrypt_1024_1_1_256_sp = &scrypt_1024_1_1_256_sp_sse2;
        printf("scrypt: using scrypt-sse2 as detected.\n");
    }
    else
    {
        scrypt_1024_1_1_256_sp = &scrypt_1024_1_1_256_sp_generic;
        printf("scrypt: using scrypt-generic, SSE2 unavailable.\n");
    }
}
#endif
#endif

void scrypt_1024_1_1_256(const char *input, char *output)
{
        char scratchpad[SCRYPT_SCRATCHPAD_SIZE];
#if defined(USE_SSE2)
        // Detection would work, but in cases where we KNOW it always has SSE2,
        // it is faster to use directly than to use a function pointer or conditional.
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_AMD64) || (defined(MAC_OSX) && defined(__i386__))
        // Always SSE2: x86_64 or Intel MacOS X
        scrypt_1024_1_1_256_sp_sse2(input, output, scratchpad);
#else
        // Detect SSE2: 32bit x86 Linux or Windows
        scrypt_1024_1_1_256_sp(input, output, scratchpad);
#endif
#else
        // Generic scrypt
        scrypt_1024_1_1_256_sp_generic(input, output, scratchpad);
#endif
}





//GN





using namespace std;
using namespace boost::assign;

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

#include "chainparamsseeds.h"

/**
 * Main network
 */

//! Convert the pnSeeds6 array into usable address objects.
static void convertSeed6(std::vector<CAddress>& vSeedsOut, const SeedSpec6* data, unsigned int count)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7 * 24 * 60 * 60;
    for (unsigned int i = 0; i < count; i++) {
        struct in6_addr ip;
        memcpy(&ip, data[i].addr, sizeof(ip));
        CAddress addr(CService(ip, data[i].port));
        addr.nTime = GetTime() - GetRand(nOneWeek) - nOneWeek;
        vSeedsOut.push_back(addr);
    }
}

//   What makes a good checkpoint block?
// + Is surrounded by blocks with reasonable timestamps
//   (no blocks before with a timestamp after, none after with
//    timestamp before)
// + Contains no strange transactions
/*
static Checkpoints::MapCheckpoints mapCheckpoints =
    boost::assign::map_list_of(259201, uint256("1c9121bf9329a6234bfd1ea2d91515f19cd96990725265253f4b164283ade5dd"))
    (424998, uint256("f31e381eedb0ed3ed65fcc98cc71f36012bee32e8efd017c4f9fb0620fd35f6b"))
    (616764, uint256("29dd0bd1c59484f290896687b4ffb6a49afa5c498caf61967c69a541f8191557")) //first block to use modifierV2
    (623933, uint256("c7aafa648a0f1450157dc93bd4d7448913a85b7448f803b4ab970d91fc2a7da7"));
*/
static Checkpoints::MapCheckpoints mapCheckpoints =
    boost::assign::map_list_of(0, uint256("0x001"));
	
static const Checkpoints::CCheckpointData data = {
    &mapCheckpoints,
    1493667067, // * UNIX timestamp of last checkpoint block
    1157185,    // * total number of transactions between genesis and last checkpoint
                //   (the tx=... number in the SetBestChain debug.log lines)
    2000        // * estimated number of transactions per day after checkpoint
};

static Checkpoints::MapCheckpoints mapCheckpointsTestnet =
    boost::assign::map_list_of(0, uint256("0x001"));
static const Checkpoints::CCheckpointData dataTestnet = {
    &mapCheckpointsTestnet,
    1454124731,
    0,
    250};

static Checkpoints::MapCheckpoints mapCheckpointsRegtest =
    boost::assign::map_list_of(0, uint256("0x001"));
static const Checkpoints::CCheckpointData dataRegtest = {
    &mapCheckpointsRegtest,
    1454124731,
    0,
    100};

class CMainParams : public CChainParams
{
public:
    CMainParams()
    {
        networkID = CBaseChainParams::MAIN;
        strNetworkID = "main";
        /**
         * The message start string is designed to be unlikely to occur in normal data.
         * The characters are rarely used upper ASCII, not valid as UTF-8, and produce
         * a large 4-byte int at any alignment.
         */
        pchMessageStart[0] = 0x88;
        pchMessageStart[1] = 0xe8;
        pchMessageStart[2] = 0xbd;
        pchMessageStart[3] = 0xfd;
        vAlertPubKey = ParseHex("0000098d3ba6ba6e7423fa5cbd6a89e0a9a5348f88d332b44a5cb1a8b7ed2c1eaa335fc8dc4f012cb8241cc0bdafd6ca70c5f5448916e4e6f511bcd746ed57dc50");
        nDefaultPort = 26285;  //17771
        bnProofOfWorkLimit = ~uint256(0) >> 15; // DMD starting difficulty is 1 / 2^12  //20
        nSubsidyHalvingInterval = 210000;
        nMaxReorganizationDepth = 100;
        nEnforceBlockUpgradeMajority = 1250;
        nRejectBlockOutdatedMajority = 1450;
        nToCheckBlockUpgradeMajority = 1500;
        nMinerThreads = 0;
        nTargetTimespan = 1 * 135; // DMD: 3 hours // 1 day
        nTargetSpacing = 1 * 135;  // DMD: 135 sec
        nLastPOWBlock = 750;
        nMaturity = 640;
        nMaturityMAX = 365*10*640;// Ten Years
        nMasternodeCountDrift = 20;
        nMasternodeColleteralLimxDev = 10000; //Params().MasternodeColleteralLimxDev()
        nModifierUpdateBlock = 1; // we use the version 2 for dmd
		nMaxMoneyOut = 21000000 * COIN;

        /*
        Z:\VMS\Gen\genquark>python genesis.py -a quark -z "Friday, July 12, 11:19 AM TIMEWARP DMDv3 1505328322 150 Tonnen Fettberg verstopft Londons Kanalsystem" -n 2083236893 -t 1505328322 -v 0
        04ffff001d01044c654672696461792c204a756c792031322c2031313a313920414d2054494d455741525020444d44763320313530353332383332322031353020546f6e6e656e2046657474626572672076657273746f706674204c6f6e646f6e73204b616e616c73797374656d
        algorithm: quark
        merkle hash: 4bc521d09b2d4a774ca97d69cbfede91fd9da2cc6662569824d36c89fb2a2578
        pszTimestamp: Friday, July 12, 11:19 AM TIMEWARP DMDv3 1505328322 150 Tonnen Fettberg verstopft Londons Kanalsystem
        pubkey: 04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f
        time: 1505328322
        bits: 0x1e0ffff0
        Searching for genesis hash..
        20894.0 hash/s, estimate: 57.1 hgenesis hash found!
        nonce: 2084038383
        genesis hash: 11298cfe767eb63ca47898a56d05b85ff7f89506f94c61d64bbe7177cae53b8a
        */
		 
        const char* pszTimestamp = "Friday, July 12, 11:19 AM TIMEWARP DMDv3 1505328322 150 Tonnen Fettberg verstopft Londons Kanalsystem";
        CMutableTransaction txNew;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.vin[0].scriptSig = CScript() << 486604799 << CScriptNum(4) << vector<unsigned char>((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].nValue = 0 * COIN;
        txNew.vout[0].scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
        genesis.vtx.push_back(txNew);
        genesis.hashPrevBlock = 0;
        genesis.hashMerkleRoot = genesis.BuildMerkleTree();
        genesis.nVersion = 1;
        genesis.nTime = 1505328322;
        genesis.nBits = 0x1e0ffff0;
        genesis.nNonce = 2084038383;






const int SCRYPT_SCRATCHPAD_SIZE = 131072 + 63;

        if (true && genesis.GetHash() != hashGenesisBlock)
        //if(false)
        {
            printf("Searching for genesis block...\n");
            // This will figure out a valid hash and Nonce if you're
            // creating a different genesis block:
            uint256 hashTarget = CBigNum().SetCompact(genesis.nBits).getuint256();
            uint256 thash;
            char scratchpad[SCRYPT_SCRATCHPAD_SIZE];

            while(true)
            {
                scrypt_1024_1_1_256_sp_generic(BEGIN(genesis.nVersion), BEGIN(thash), scratchpad);
                if (thash <= hashTarget)
                    break;
                if ((genesis.nNonce & 0xFFF) == 0)
                {
                    printf("nonce %08X: hash = %s (target = %s)\n", genesis.nNonce, thash.ToString().c_str(), hashTarget.ToString().c_str());
                }
                ++genesis.nNonce;
                if (genesis.nNonce == 0)
                {
                    printf("NONCE WRAPPED, incrementing time\n");
                    ++genesis.nTime;
                }
            }
            printf("block.nTime = %u \n", genesis.nTime);
            printf("block.nNonce = %u \n", genesis.nNonce);
            printf("block.GetHash = %s\n", genesis.GetHash().ToString().c_str());
            printf("genesis.hashMerkleRoot = %s \n", genesis.hashMerkleRoot.ToString().c_str()); //improvised. worked for me, to find merkle root

            }

//            printf("genesis.nTime = %u \n", genesis.nTime);
//            printf("genesis.nNonce = %u \n", genesis.nNonce);
//            printf("genesis.nVersion = %u \n", genesis.nVersion);
//            printf("genesis.GetHash = %s\n", genesis.GetHash().ToString().c_str()); //first this, then comment this line out and uncomment the one under.
//            printf("genesis.hashMerkleRoot = %s \n", genesis.hashMerkleRoot.ToString().c_str()); //improvised. worked for me, to find merkle root
//            printf("min nBit:  %08x\n", bnProofOfWorkLimit.GetCompact());





        hashGenesisBlock = genesis.GetHash();
        //assert(hashGenesisBlock == uint256("0x0000029b550c0095513d9bb9dd14f88442573baca94d70e49018a510979c0f9b"));
        //assert(genesis.hashMerkleRoot == uint256("0x4bc521d09b2d4a774ca97d69cbfede91fd9da2cc6662569824d36c89fb2a2578"));
        assert(hashGenesisBlock == uint256("0x"));
        assert(genesis.hashMerkleRoot == uint256("0x"));

        vFixedSeeds.clear();
        vSeeds.clear();
	    vSeeds.push_back(CDNSSeedData("dnsseed.bit.diamonds", "dnsseed.bit.diamonds"));
		vSeeds.push_back(CDNSSeedData("37.120.186.85", "37.120.186.85"));
		vSeeds.push_back(CDNSSeedData("185.194.140.60", "185.194.140.60"));
		vSeeds.push_back(CDNSSeedData("188.68.39.1", "188.68.39.1"));
		vSeeds.push_back(CDNSSeedData("188.68.52.172", "188.68.52.172"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 90);
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 8);
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 218);
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x04)(0x88)(0xB2)(0x1E).convert_to_container<std::vector<unsigned char> >();
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x04)(0x88)(0xAD)(0xE4).convert_to_container<std::vector<unsigned char> >();
        // 	BIP44 coin type is from https://github.com/satoshilabs/slips/blob/master/slip-0044.md 109 	0x8000006d
        base58Prefixes[EXT_COIN_TYPE] = boost::assign::list_of(0x80)(0x00)(0x00)(0x6d).convert_to_container<std::vector<unsigned char> >();

        convertSeed6(vFixedSeeds, pnSeed6_main, ARRAYLEN(pnSeed6_main));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = false; //true
        fAllowMinDifficultyBlocks = true; //true
        fDefaultConsistencyChecks = false;
        fRequireStandard = false; // true
        fMineBlocksOnDemand = false; //false
        fSkipProofOfWorkCheck = false;
        fTestnetToBeDeprecatedFieldRPC = false;
        fHeadersFirstSyncingActive = false;
		
	/*
        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false; 
        fTestnetToBeDeprecatedFieldRPC = true;
	*/

        nPoolMaxTransactions = 3;
        strSporkKey = "049DAC995F6CF5A3ECFC63BF1BA51DEDE08ED42F3713075F145853C8CA51D3374870C2D32FFD76B5D754EB9BFDF5B1C01B0666D2A71EDE10166094063FBE010C87";
        //strSporkKey = "04B433E6598390C992F4F022F20D3B4CBBE691652EE7C48243B81701CBDB7CC7D7BF0EE09E154E6FCBF2043D65AF4E9E97B89B5DBAF830D83B9B7F469A6C45A717";
        strmixTXPoolDummyAddress = "dLgDfmmqMYsjL4RErF7CRvWd2A8rcauJPh";
        nStartMasternodePayments = 1514332800; //12/27/2017 @ 12:00am (UTC)
    }

    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        return data;
    }
};
static CMainParams mainParams;

/**
 * Testnet (v3)
 */
class CTestNetParams : public CMainParams
{
public:
    CTestNetParams()
    {
        networkID = CBaseChainParams::TESTNET;
        strNetworkID = "test";
        pchMessageStart[0] = 0x45;
        pchMessageStart[1] = 0x76;
        pchMessageStart[2] = 0x65;
        pchMessageStart[3] = 0xba;
        vAlertPubKey = ParseHex("000010e83b2703ccf322f7dbd62dd5855ac7c10bd055814ce121ba32607d573b8810c02c0582aed05b4deb9c4b77b26d92428c61256cd42774babea0a073b2ed0c9");
        nDefaultPort = 51474;
        nEnforceBlockUpgradeMajority = 51;
        nRejectBlockOutdatedMajority = 75;
        nToCheckBlockUpgradeMajority = 100;
        nMinerThreads = 0;
        nTargetTimespan = 1 * 135; // DMD: 1 day
        nTargetSpacing = 1 * 135;  // DMD: 1 minute
        nLastPOWBlock = 200;
        nMaturity = 15;
        nModifierUpdateBlock = 51197; //approx Mon, 17 Apr 2017 04:00:00 GMT
        nMaxMoneyOut = 43199500 * COIN;
		

        //! Modify the testnet genesis block so the timestamp is valid for a later start.
        genesis.nTime = 1505328322;
        genesis.nNonce = 2084038383;

        hashGenesisBlock = genesis.GetHash();
        assert(hashGenesisBlock == uint256("0x0000029b550c0095513d9bb9dd14f88442573baca94d70e49018a510979c0f9b"));

        vFixedSeeds.clear();
        vSeeds.clear();
	vSeeds.push_back(CDNSSeedData("dnsseed.bit.diamonds", "dnsseed.bit.diamonds"));

        base58Prefixes[PUBKEY_ADDRESS] = std::vector<unsigned char>(1, 139); // Testnet diamond addresses start with 'x' or 'y'
        base58Prefixes[SCRIPT_ADDRESS] = std::vector<unsigned char>(1, 19);  // Testnet diamond script addresses start with '8' or '9'
        base58Prefixes[SECRET_KEY] = std::vector<unsigned char>(1, 239);     // Testnet private keys start with '9' or 'c' (Bitcoin defaults)
        // Testnet diamond BIP32 pubkeys start with 'DRKV'
        base58Prefixes[EXT_PUBLIC_KEY] = boost::assign::list_of(0x3a)(0x80)(0x61)(0xa0).convert_to_container<std::vector<unsigned char> >();
        // Testnet diamond BIP32 prvkeys start with 'DRKP'
        base58Prefixes[EXT_SECRET_KEY] = boost::assign::list_of(0x3a)(0x80)(0x58)(0x37).convert_to_container<std::vector<unsigned char> >();
        // Testnet diamond BIP44 coin type is '1' (All coin's testnet default)
        base58Prefixes[EXT_COIN_TYPE] = boost::assign::list_of(0x80)(0x00)(0x00)(0x01).convert_to_container<std::vector<unsigned char> >();

        convertSeed6(vFixedSeeds, pnSeed6_test, ARRAYLEN(pnSeed6_test));

        fRequireRPCPassword = true;
        fMiningRequiresPeers = true;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = false;
        fRequireStandard = false;
        fMineBlocksOnDemand = false; 
        fTestnetToBeDeprecatedFieldRPC = true;

        nPoolMaxTransactions = 2;
        strSporkKey = "04348C2F50F90267E64FACC65BFDC9D0EB147D090872FB97ABAE92E9A36E6CA60983E28E741F8E7277B11A7479B626AC115BA31463AC48178A5075C5A9319D4A38";
        strmixTXPoolDummyAddress = "y57cqfGRkekRyDRNeJiLtYVEbvhXrNbmox";
        nStartMasternodePayments = 1420837558; //Fri, 09 Jan 2015 21:05:58 GMT
    }
    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        return dataTestnet;
    }
};
static CTestNetParams testNetParams;

/**
 * Regression test
 */
class CRegTestParams : public CTestNetParams
{
public:
    CRegTestParams()
    {
        networkID = CBaseChainParams::REGTEST;
        strNetworkID = "regtest";
        strNetworkID = "regtest";
        pchMessageStart[0] = 0xa1;
        pchMessageStart[1] = 0xcf;
        pchMessageStart[2] = 0x7e;
        pchMessageStart[3] = 0xac;
        nSubsidyHalvingInterval = 150;
        nEnforceBlockUpgradeMajority = 750;
        nRejectBlockOutdatedMajority = 950;
        nToCheckBlockUpgradeMajority = 1000;
        nMinerThreads = 1;
        nTargetTimespan = 24 * 60 * 60; // Diamond: 1 day
        nTargetSpacing = 1 * 60;        // Diamond: 1 minutes
        bnProofOfWorkLimit = ~uint256(0) >> 1;
        genesis.nTime = 1505328322;
        genesis.nBits = 0x1e0ffff0;
        genesis.nNonce = 2084038383;

        hashGenesisBlock = genesis.GetHash();
        nDefaultPort = 51476;
        assert(hashGenesisBlock == uint256("0x0000029b550c0095513d9bb9dd14f88442573baca94d70e49018a510979c0f9b"));

        vFixedSeeds.clear(); //! Testnet mode doesn't have any fixed seeds.
        vSeeds.clear();      //! Testnet mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fAllowMinDifficultyBlocks = true;
        fDefaultConsistencyChecks = true;
        fRequireStandard = false;
        fMineBlocksOnDemand = true;
        fTestnetToBeDeprecatedFieldRPC = false;
    }
    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        return dataRegtest;
    }
};
static CRegTestParams regTestParams;

/**
 * Unit test
 */
class CUnitTestParams : public CMainParams, public CModifiableParams
{
public:
    CUnitTestParams()
    {
        networkID = CBaseChainParams::UNITTEST;
        strNetworkID = "unittest";
        nDefaultPort = 51478;
        vFixedSeeds.clear(); //! Unit test mode doesn't have any fixed seeds.
        vSeeds.clear();      //! Unit test mode doesn't have any DNS seeds.

        fRequireRPCPassword = false;
        fMiningRequiresPeers = false;
        fDefaultConsistencyChecks = true;
        fAllowMinDifficultyBlocks = false;
        fMineBlocksOnDemand = true;
    }

    const Checkpoints::CCheckpointData& Checkpoints() const
    {
        // UnitTest share the same checkpoints as MAIN
        return data;
    }

    //! Published setters to allow changing values in unit test cases
    virtual void setSubsidyHalvingInterval(int anSubsidyHalvingInterval) { nSubsidyHalvingInterval = anSubsidyHalvingInterval; }
    virtual void setEnforceBlockUpgradeMajority(int anEnforceBlockUpgradeMajority) { nEnforceBlockUpgradeMajority = anEnforceBlockUpgradeMajority; }
    virtual void setRejectBlockOutdatedMajority(int anRejectBlockOutdatedMajority) { nRejectBlockOutdatedMajority = anRejectBlockOutdatedMajority; }
    virtual void setToCheckBlockUpgradeMajority(int anToCheckBlockUpgradeMajority) { nToCheckBlockUpgradeMajority = anToCheckBlockUpgradeMajority; }
    virtual void setDefaultConsistencyChecks(bool afDefaultConsistencyChecks) { fDefaultConsistencyChecks = afDefaultConsistencyChecks; }
    virtual void setAllowMinDifficultyBlocks(bool afAllowMinDifficultyBlocks) { fAllowMinDifficultyBlocks = afAllowMinDifficultyBlocks; }
    virtual void setSkipProofOfWorkCheck(bool afSkipProofOfWorkCheck) { fSkipProofOfWorkCheck = afSkipProofOfWorkCheck; }
};
static CUnitTestParams unitTestParams;


static CChainParams* pCurrentParams = 0;

CModifiableParams* ModifiableParams()
{
    assert(pCurrentParams);
    assert(pCurrentParams == &unitTestParams);
    return (CModifiableParams*)&unitTestParams;
}

const CChainParams& Params()
{
    assert(pCurrentParams);
    return *pCurrentParams;
}

CChainParams& Params(CBaseChainParams::Network network)
{
    switch (network) {
    case CBaseChainParams::MAIN:
        return mainParams;
    case CBaseChainParams::TESTNET:
        return testNetParams;
    case CBaseChainParams::REGTEST:
        return regTestParams;
    case CBaseChainParams::UNITTEST:
        return unitTestParams;
    default:
        assert(false && "Unimplemented network");
        return mainParams;
    }
}

void SelectParams(CBaseChainParams::Network network)
{
    SelectBaseParams(network);
    pCurrentParams = &Params(network);
}

bool SelectParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::MAX_NETWORK_TYPES)
        return false;

    SelectParams(network);
    return true;
}
