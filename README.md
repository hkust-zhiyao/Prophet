# Profile-Guided Temporal Prefetching
Mengming Li, Qijun Zhang, Yichuan Gao, Wenji Fang, Yao Lu, Yongqing Ren, Zhiyao Xie

```
@inproceedings{li2025profile,
  title={Profile-Guided Temporal Prefetching},
  author={Li, Mengming and Zhang, Qijun and Gao, Yichuan and Fang, Wenji and Lu, Yao and Ren, Yongqing and Xie, Zhiyao},
  booktitle={Proceedings of the 52nd Annual International Symposium on Computer Architecture},
  pages={572--585},
  year={2025}
}
```

# Compilation
```
scons build/RISCV/gem5.opt
```
# Evaluation

This repository is built on [Xiangshan's gem5](https://github.com/OpenXiangShan/GEM5). We use the same [SimPoint](https://docs.xiangshan.cc/zh-cn/latest/tools/simpoint/) methodology as Xiangshan to evaluate Prophet.


## Checkpoint Generation

Generation flow: https://docs.xiangshan.cc/zh-cn/latest/tools/simpoint/

Primary tool: [NEMU](https://github.com/OpenXiangShan/NEMU)

## Experiment Flow

### Step 1: Profiling

Modify the `train` parameter for the class `TriagePGOPrefetcher` to `true` in the following files:

```
$GEM5_PATH/src/mem/cache/prefetch/Prefetcher.py
```
Then, run gem5 with the following example command:
```
$GEM5_PATH/build/RISCV/gem5.opt --outdir=$OUT_DIR $GEM5_PATH/configs/example/fs.py \
--num-cpus=1 --cpu-type DerivO3CPU --mem-size 8GB --mem-type=LPDDR5_5500_1x16_BG_BL32 \
--caches --cacheline_size=64 --l1i_size=64kB --l1i_assoc=4 --l1d_size=64kB --l1d_assoc=4 \
--l2cache --l2_size=512kB --l2_assoc=8 --l3cache --l3_size=2MB --l3_assoc=16 \
--bp-type=LTAGE --l1d-hwp-type=StridePrefetcher --l2-hwp-type=TriagePGOPrefetcher \
--generic-rv-cpt $CKPT_PATH/omnetpp_492740000000_0.497511/0/_492740000000_.gz --xiangshan-system -I $TOTAL_INST --warmup-insts-no-switch $WARM_UP_INST
```
A file named `train.txt` will be generated in the `$OUT_DIR`. The subsequent procedures are based on this generated file.

### Step 2.1: Analysis

A single workload contains multiple checkpoints, and each checkpoint generates its own `train.txt` file. First, merge all `train.txt` files from the same workload to simulate a realistic PGO setting.

```
cd $GEM5_PATH/pgo
python3 merge.py $RESULT_DIR
```

Then, we generate the hints for temporal prefetchers.
```
cd $GEM5_PATH/pgo
python3 batch_process_new.py $RESULT_DIR 1
```

Finally, a directory named `profile_re` is generated in the current directory. We use this information to guide the temporal prefetcher, as described in the paper.

### Step 2.2: Running

Set the `train` parameter for the `TriagePGOPrefetcher` class to `false`, and then run the workloads with gem5. Gem5 will automatically read the hints from `profile_re`.  

**Note:** Before the first run, update the path in `$GEM5_PATH/src/mem/cache/prefetch/triage_pgo.cc`.

### Step 3: Learning

First, run a workload with all its inputs to simulate the input sampling procedure (Dont forget to set the `train` parameter to `true`). For example, run `gcc` with all its inputs. Then, we generate the corresponding hints:
```
cd $GEM5_PATH/pgo
python3 learn.py $RESULT_DIR
```

**Note:** The key in this step is to modify the `inp = []` parameter in `learn.py`.  For example, to let Prophet learn from only two inputs of `gcc` (e.g., `gcc_166` and `gcc_expr`), update `inp` as follows:  

```python
inp = ['gcc_166', 'gcc_expr']
```
Finally, generate the learned hints in `profile_re`, and refer to **Step 3** to run the target workloads.



