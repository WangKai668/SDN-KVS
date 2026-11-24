# SDN-KVS 终稿实验复现说明

本说明对应论文图 7–14 和表 3–4。终稿批次位于 NAS 的 `/mnt/nasDisk_ds3617/wangkai/SDN-KVS/statistic/` 下。每次新建 `paper-final-时间戳` 目录。终稿使用新跑的原始日志生成数据表和图稿。

## 工程与配置

仿真入口为 `scratch/sdn-kvs/sdn-kvs-main.cc`，请求生成、网络拓扑和 `--controllerRank` 均在此配置。控制器排序和转发在 `scratch/sdn-kvs/controller.cc`，服务器处理在 `scratch/sdn-kvs/server.cc`，交接统计在 `scratch/sdn-kvs/handover-stats.cc`。环境为 ns-3.39 optimized、OFSwitch13 5.2.2（`ab3ff7b2b5b7ecd6c9af5c3f3abacafc9b384913` 加工作区补丁）、BOFUSS（`1b6106e3e3814c50b323e43b6bb2dec5ab13ffe7`）。具体 Git 提交、工作区补丁、编译器、CPU 和仿真二进制摘要见 `provenance/`。

| 场景 | 客户端 × 服务器 | 工作负载 | 服务器速率比例（1、2、3 keys/ms） |
|---|---:|---|---|
| 8 | 32 × 32 | 泊松请求大小、轻键集 | 1:0:0 |
| 9 | 32 × 32 | 泊松请求大小、轻键集 | 1:1:0 |
| 11 | 32 × 32 | 截断 Pareto 请求大小、重键集 | 1:0:0 |
| 12 | 32 × 32 | 截断 Pareto 请求大小、重键集 | 1:1:0 |
| 14 | 128 × 128 | 泊松请求大小、轻键集 | 1:1:0 |
| 15 | 128 × 128 | 截断 Pareto 请求大小、重键集 | 1:1:0 |

共有设置为 150,000 个逻辑键、Zipf 目标选择参数 1、指数请求间隔、100 Gbit/s CSMA 数据链路和 1,000 s 的仿真停止时刻。轻尾请求大小的泊松均值为 80。Pareto 的 Scale、Shape、Bound 分别为 40、2、80。基准目标负载为 0.7，基准数据链路传播时延为 600 ns。SDN-KVS 使用静态 `QT_s=64`，图 7 扫描十档阈值，图 9 扫描链路延时，图 13 与表 4 扫描目标负载。FIFO、SRPT、SBF、DAS 的旧脚本占位阈值为 6。图 10–12 与图 14 的 SRPT、SBF 均为服务器端基线。表 3 的集中式变体通过 `--algorithm=4` 和 `--controllerRank` 选择。

默认随机设置的运行命令省略 `--RngSeed` 和 `--RngRun`，ns-3 默认值均为 1。重复实验显式使用种子 1701–1705，各场景同种子的算法组共享相同请求表和服务器速率映射，逐次 CSV 保存二者的 SHA-256。源代码内固定的 C++ 生成器沿用当前设置。

## 图表、脚本和备份文件

所有路径均相对于终稿结果目录。每个原始结果目录包含 `output_*.log`、实际 `.command`、退出状态 `.exit`、`export.log` 和单次 `study_manifest.csv`。下表的旧脚本列指出原工作流入口，终稿统一使用 `export_paper.sh` 批量调用现有 `run.sh` 路径，并由 `analyze_paper.py` 复用 `analysis.py` 的事件解析与 `analyze_controller_overhead.py` 的运行校验。

| 论文图表 | 运行设置 | 原仿真与数据处理入口 | 终稿原始日志位置 | 终稿数据和图稿 |
|---|---|---|---|---|
| 图 7a、7b | 场景 8，默认随机设置，SDN-KVS 扫 `QT_s=4,8,16,32,64,128,256,512,1024,2048`，每档 1 次，计时阶段在本批次内串行 | `export_tqs.sh`、`analyze_tqs.sh`、`analyze_controller_overhead.py` | `raw/qscan/` | `data/fig07_thresholds.csv`、`python_figures/fig07_threshold_sensitivity.pdf/.png` |
| 图 8 | 场景 8，默认随机设置，取图 7 的 `QT_s=64` 运行 | `export_tqs.sh`、`draw_wait_time.py` | `raw/qscan/s8_a4_q64_l600_u0.7_run1_bp-lpt/` | `data/fig08_keyset_wait_ms.csv`、`python_figures/fig08_keyset_waiting.pdf/.png` |
| 图 9 | 场景 8，默认随机设置，仅 SDN-KVS。0.6、1、2.5、5、10、20、40、50 µs，各 1 次 | `export_latency.sh`、`analyze_latency.sh`、`analyze_parseToOriginUtilLatency.py` | `raw/latency/` | `data/fig09_latency_origin.csv`、`data/origin_input.xlsx` 的 Fig9 |
| 图 10 | 场景 8，五算法 × 五种子 | `export_tqs.sh`、`analyze_tqs.sh`、`analyze_sdn_kvs_study.py` | `raw/repeat/s8_*/` | `data/fig10_origin_mean_sd_ms.csv`、Origin 工作表 FIG10 |
| 表 3 | 场景 8，默认随机设置，SDN-KVS、Centralized SBF、Centralized SRPT 各 1 次 | `export_tqs.sh`、`run.sh` 传递仿真参数 `--controllerRank`、`analyze_controller_overhead.py` | 基线取图 7 的 `QT_s=64`，变体取 `raw/ablation/` | `data/table03_ablation.csv` |
| 图 11 | 场景 9，五算法 × 五种子 | `export_tqs.sh`、`analyze_tqs.sh`、`analyze_sdn_kvs_study.py` | `raw/repeat/s9_*/` | `data/fig11_origin_mean_sd_ms.csv`、Origin 工作表 FIG11 |
| 图 12a、12b | 场景 11、12 各五算法 × 五种子 | `export_tqs.sh`、`analyze_tqs.sh`、`analyze_sdn_kvs_study.py` | `raw/repeat/s11_*/`、`raw/repeat/s12_*/` | `data/fig12a_origin_mean_sd_ms.csv`、`data/fig12b_origin_mean_sd_ms.csv`，Origin 工作表 FIG12A、FIG12B |
| 图 13 | 场景 9，默认随机设置，五算法分别扫 0.4、0.5、0.6、0.71、0.8、0.9 | `export_uti.sh`、`analyze_util.sh`、`analyze_parseToOriginUtilLatency.py` | `raw/utilization/` 下上述六档 | `data/fig13_mean_ms_origin.csv`、`data/fig13_p95_ms_origin.csv`，Origin 工作表 Fig13a、Fig13b |
| 表 4 | 场景 9，默认随机设置，仅 SDN-KVS。复用图 13 的六档，另补 1.00 | `export_uti.sh` 与 `run.sh` 的交接统计开关、`analyze_paper.py` 汇总逐轮 CSV | `raw/utilization/` 下算法编号 4 的七档，以及同目录 `*_handover_rounds.csv` | `data/table04_handover.csv` |
| 图 14 | 场景 14、15，默认随机设置，各五算法 × 1 次 | `export_tqs.sh`、`analyze_tqs.sh`、`analyze_parseToOriginTqs.py` | `raw/large/` | `data/fig14_scene{14,15}_raw_ms.csv` 和 `data/fig14_scene{14,15}_origin_normalized.csv`，Origin 工作表 Fig14a、Fig14b |

共安排 161 次独立仿真。10 次阈值扫描覆盖图 7 并提供图 8 与表 3 基线，图 9 共 8 次，图 10–12 共 100 次，表 3 两个变体共 2 次，图 13 与表 4 合计 31 次，图 14 共 10 次。

## 统计和绘图口径

- 请求响应时间以该请求最后一个 keyset 的客户端接收时间为终点。起点取请求的第一次实际发送时间，DAS 使用请求表中的计划到达时间以纳入客户端排队。每次运行在请求级计算 Mean、P95、P99，分位数用 NumPy 默认线性插值。
- 图 7a 的开销曲线为控制器等待时间非零的 keyset 数占全部 keyset 的比例。图 7b 的调度时间为排序调用的主机墙钟耗时之和，CSV 用 ms，折线用 s。堆叠柱为发出的过载信号和流表重定向、恢复消息，图中单位为千条。墙钟计时受运行主机影响。
- 图 8 的控制器等待按转发提交时刻减控制器入队时刻。服务器等待按服务器发响应时刻减收到请求时刻再减理论服务时长，服务时长等于 keyset 大小除以服务器速率。CSV 保存全部 keyset 的计算值，图的横轴显示 0–20,000，纵轴显示 0–500 ms，采用原 `draw_wait_time.py` 的 x 的 1.2 次方 显示尺度。
- 图 9 的八档统一使用此前验证过的 32,768 包数据链路队列，包含普通 CSMA 与 OFSwitch13 出口。每档核验队列、PHY、OpenFlow 丢包跟踪和请求完整性。本次扫描中只有传播时延随档位变化。
- 图 10–12 对五次运行各自的 Mean、P95、P99 求算术均值与样本标准差，标准差分母为 4，单位均为 ms。Origin 使用五组并排柱，后五列设置为对应 YErr，纵轴用 log₂。
- 表 3 中 Centralized SBF 按完整请求的最大 keyset 大小排序，Centralized SRPT 按各目标服务器分别对单个 keyset 按大小排序并独立发送。
- 图 13 的目标负载输入为 `0.71`，论文截图该刻度表示为 `0.7`。终稿表格和预览保留实际输入值。图 13 展示 Mean、P95，归档另有 P99。
- 表 4 对所有服务器的有效交接轮次空闲时间求和。以日志中的仿真总时长为分母，同时导出百分比和万分比。论文表列使用万分比（‱），即总空闲 ms 除以 1,000,000 ms 后乘以 10,000。未完成、无待转发数据和无法关联的轮次分别在 CSV 里提供计数。
- 图 14 每个场景和指标分别用各算法的响应时间除以同场景、同指标的 SDN-KVS 响应时间，SDN-KVS 基准为 1。原始 ms 值也保存在归档。

图 7 与图 8 为 Python 正式图稿，绘图代码为 `plot_paper.py`。图 9–14 的指定 Origin 图使用 `data/origin_input.xlsx` 或同名 CSV 导入，工作表注明 X、Y、YErr。图 9 采用等间距延时标签和 log₂ 纵轴，图 13 为 Mean/P95 两个折线子图、log₂ 纵轴，图 14 为两组归一化柱图及 `y=1` 基准。运行主机无 Origin 应用，`origin_preview/` 提供同批数据的 Python 校对图。原生 Origin 工程需在安装 Origin 的机器上按工作表生成。

原表 4 截图中的空闲时间与利用率归档 `JNCA-major-revision 20261121/util-handover-20261121-184145/handover_idle_by_utilization.csv` 四舍五入后的数值一致。该归档使用显式种子 1701。此次按终稿要求使用默认随机设置并重新计算表 4，排版时读取新批次的 `data/table04_handover.csv` 或 `data/tables_03_04.md`。

## 复现命令

从仓库根目录运行，需要 Bash、Python 3.12、NumPy、Matplotlib、openpyxl 和项目现有 C++ 构建环境。目标必须是新的目录。计时结束后默认最多并行 8 个非计时比较任务，可用 `PAPER_SIM_JOBS` 调整。

```bash
batch="/mnt/nasDisk_ds3617/wangkai/SDN-KVS/statistic/paper-final-$(date +%Y%m%d-%H%M%S)"
bash results/paper-final-tools/export_paper.sh "$batch"
PYTHONPATH="$PWD" python3.12 results/paper-final-tools/analyze_paper.py "$batch"
```

归档中的脚本也可复用。从仓库根目录执行时，将上述两个脚本的路径换成现有备份目录的 `provenance/export_paper.sh` 和 `provenance/analyze_paper.py`。分析入口读取 `run_plan.csv` 和各任务清单，写出 `data/study_runs.csv`、按组整理的 `data/result_*.log`、图表数据及图稿。正式结果需通过全部 161 次运行的完成检查；无效运行及原因能在逐次 CSV 中定位。

## 备份和核查

- `run_plan.csv` 是终稿任务索引，包含场景、算法、静态阈值、延时、目标负载、种子、控制器排序和队列配置。
- `data/study_runs.csv` 是逐次结果索引，`log_file` 指向实际仿真日志。论文表格可直接读取 `data/tables_03_04.md`，其中的显示精度对应论文排版。
- `python_figures/` 保存图 7、8 的 PDF 和 400 dpi PNG。 `origin_preview/` 供校对，`data/origin_input.xlsx` 提供 Origin 源工作表。
- `provenance/` 保存此次执行的调度脚本、统计和绘图源码、配置、工作区补丁、仿真应用源码快照和二进制 SHA-256。
- 批次同级的 `paper-final-时间戳.tar.gz` 打包原始日志、处理结果、图稿和说明，旁边的 `.sha256` 用于验证。下载时将压缩包与校验文件放在一起，执行 `sha256sum -c paper-final-时间戳.tar.gz.sha256`。
