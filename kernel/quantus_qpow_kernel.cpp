/**
 * Ascend C Kernel skeleton for Quantus QPoW
 * (Batch GEMM + UB ping-pong + AIC/AIV).
 * Not required for the CPU path that can submit shares.
 */
#include "kernel_operator.h"
using namespace AscendC;

constexpr int32_t BATCH_NONCE = 1024;
constexpr int32_t STATE_DIM = 16;
constexpr int32_t TILE_ELEMENTS = BATCH_NONCE * STATE_DIM;

class QuantusQPoWKernel {
public:
    __aicore__ inline QuantusQPoWKernel() {}
    __aicore__ inline void Init(GM_ADDR nonces_in, GM_ADDR mds_matrix, GM_ADDR hashes_out) {
        nonceGM.SetGlobalBuffer((__gm__ uint32_t*)nonces_in);
        mdsGM.SetGlobalBuffer((__gm__ uint32_t*)mds_matrix);
        outGM.SetGlobalBuffer((__gm__ uint32_t*)hashes_out);
        pipe.InitBuffer(inQueueUB, 2, TILE_ELEMENTS * sizeof(uint32_t));
        pipe.InitBuffer(outQueueUB, 2, TILE_ELEMENTS * sizeof(uint32_t));
        pipe.InitBuffer(mdsBufferUB, 1, STATE_DIM * STATE_DIM * sizeof(uint32_t));
        LocalTensor<uint32_t> mdsLocal = mdsBufferUB.Get<uint32_t>();
        DataCopy(mdsLocal, mdsGM, STATE_DIM * STATE_DIM);
    }
    __aicore__ inline void Process(int32_t total_tiles) {
        for (int32_t t = 0; t < total_tiles; ++t) {
            LocalTensor<uint32_t> inLocal = inQueueUB.AllocTensor<uint32_t>();
            DataCopy(inLocal, nonceGM[t * TILE_ELEMENTS], TILE_ELEMENTS);
            inQueueUB.EnQueue(inLocal);
            LocalTensor<uint32_t> computeTensor = inQueueUB.DeQueue<uint32_t>();
            LocalTensor<uint32_t> outLocal = outQueueUB.AllocTensor<uint32_t>();
            // MatMul + Vector S-box/modulo TODO
            outQueueUB.EnQueue(outLocal);
            LocalTensor<uint32_t> result = outQueueUB.DeQueue<uint32_t>();
            DataCopy(outGM[t * TILE_ELEMENTS], result, TILE_ELEMENTS);
            inQueueUB.FreeTensor(computeTensor);
            outQueueUB.FreeTensor(result);
        }
    }
private:
    TPipe pipe;
    TQue<QuePosition::VECIN, 2> inQueueUB;
    TQue<QuePosition::VECOUT, 2> outQueueUB;
    TBuf<QuePosition::VECCAL> mdsBufferUB;
    GlobalTensor<uint32_t> nonceGM, mdsGM, outGM;
};

extern "C" __global__ __aicore__ void quantus_qpow_mining_kernel(
    GM_ADDR nonces_in, GM_ADDR mds_matrix, GM_ADDR hashes_out, int32_t total_tiles) {
    QuantusQPoWKernel op;
    op.Init(nonces_in, mds_matrix, hashes_out);
    op.Process(total_tiles);
}
