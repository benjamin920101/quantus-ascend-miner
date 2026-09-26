# Quantus (QTC) QPoW Miner — CPU Poseidon2 + Kryptex Stratum

## 狀態

此版本實作：

1. **正確規格的 Poseidon2**（Goldilocks 體、`hash_squeeze_twice`、官方常數）
2. **Stratum 連線** Kryptex (`qtc.kryptex.network:7049`)
3. **目標比對**（digest < target）後 **自動提交 share**
4. Ascend NPU kernel 仍保留為實驗路徑（非本版挖礦主路徑）

CPU 算力約數 kH/s～數十 kH/s 量級，主要用於驗證協議與 share 流程。  
要有競爭力算力請使用官方 / SRBMiner / KRig 等 GPU miner。

## 編譯

```bash
chmod +x scripts/build.sh
./scripts/build.sh
# 或
mkdir build && cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j
```

## 執行

```bash
./quantus_ascend_miner \
  --pool stratum+tcp://qtc.kryptex.network:7049 \
  --wallet qzYourQuantusAddress \
  --worker my-rig
```

區域節點範例：`qtc-eu.kryptex.network:7049`、`qtc-us.kryptex.network:7049` 等。

## 重要說明

- **Share 是否被接受**取決於：
  1. Poseidon2 實作與官方 `qp-poseidon-core` 完全一致
  2. 礦池 job 欄位（`mining_hash` / `target`）解析正確
  3. submit 參數格式符合 Kryptex 實際協議

- 若 pool 使用特殊 Stratum 方言（例如 LuckyPool 風格的 `job` 物件），可能需要微調 `stratum_client.cpp` 的解析與 submit 欄位。

- 本專案的 Poseidon2 已依 `qp-poseidon-core` 公開常數與結構移植；若仍出現大量 reject，請用官方 Rust crate 做單元測試對齊，或改用 FFI 呼叫 `qp-poseidon`。

## 授權

MIT — 僅供研究。挖礦收益與風險由使用者自負。

## GitHub Actions / CANN 9.1.0

`.github/workflows/build.yml` 參考 [ascend_kas](https://github.com) 風格：

| Job | Runner | 說明 |
|-----|--------|------|
| `build-host` | `ubuntu-latest` (x86_64) | 只編 CPU Poseidon2 + Stratum，不需 CANN |
| `build-with-cann` | `ubuntu-22.04-arm` | **自動 wget CANN 9.1.0** 並安裝後再編譯 |

CANN 下載來源（與 ascend_kas 相同 mirror）：

```
https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/legacy/20260720000024864/Ascend-cann-toolkit_9.1.0_linux-aarch64.run
```

安裝指令：`bash Ascend-cann-toolkit_9.1.0_linux-aarch64.run --install --quiet`

觸發方式：push / PR / **workflow_dispatch**（Actions 頁面手動跑）。
成功後可在 Artifacts 下載 `quantus-ascend-miner-linux-aarch64-*`。
