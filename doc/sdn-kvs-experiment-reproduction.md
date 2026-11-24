# SDN-KVS 延时、阈值与控制器消融实验复现

从仓库根目录运行。实验使用 ns-3.39 的 optimized 构建及当前 OFSwitch13/BOFUSS 依赖。分析需要 Python 3.12、NumPy 和 Matplotlib。每个导出命令接收一个尚未创建的结果目录，并依次执行仿真。运行计时实验时保持同一主机与构建配置。

## 延时敏感性

```bash
bash export_latency.sh --study results/latency-study
bash analyze_latency.sh --study results/latency-study
```

该模式沿用 32 个客户端、32 台同构服务器、150,000 个逻辑键、轻尾、热点开启、指数间隔和 0.7 目标利用率。FIFO、SRPT、SBF、DAS 和 SDN-KVS 各运行 11 个链路传播延时。延时参数以 ns 传入，依次为 600、1000、2500、5000、10000、20000、40000、50000、100000、500000、1000000。所有运行使用 ns-3 种子 1701，SDN-KVS 采用静态阈值 64。输出 `latency_summary.csv` 和五算法、仅 SDN-KVS 两组 PDF/PNG 图。每个延时点只有一次运行，方差列为空。

## 场景 8–13 五次重复与图 7

```bash
bash export_tqs.sh --study results/tqs-study
bash analyze_tqs.sh --study results/tqs-study
```

所有场景都使用 32 个客户端、32 台服务器、150,000 个逻辑键、热点模式、指数间隔、0.7 目标利用率和 600 ns 链路传播延时。当前代码的请求大小参数是 80，键集倍率为 1。

| 场景 | 请求与键集 | 服务速率档位比例（1、2、3 keys/ms） | SDN-KVS 静态阈值 |
|---|---|---|---|
| 8 | 泊松、轻键集 | 1:0:0 | 4、8、16、32、64、128、256、512、1024、2048 |
| 9 | 泊松、轻键集 | 1:1:0 | 64 |
| 10 | 泊松、轻键集 | 5:2:1 | 64 |
| 11 | 截断 Pareto、重键集 | 1:0:0 | 64 |
| 12 | 截断 Pareto、重键集 | 1:1:0 | 64 |
| 13 | 截断 Pareto、重键集 | 5:2:1 | 64 |

五个重复使用 ns-3 种子 1701 至 1705。`run.sh` 对不同算法和阈值传入同一个重复编号及种子。场景内要求同种子的请求表与服务器速率映射哈希一致，同时检查不同种子的请求表发生变化。C++ 的固定 `mt19937`、`rand()` 和服务器速率洗牌沿用原设置。

`tqs_summary.csv` 报告每个场景和算法的 Mean、P95、P99。每个指标先按单次完整运行计算，再对五次运行的统计量求均值和样本方差。方差的分母为有效运行数减一。`figure7_summary.csv` 和 `figure7_seeded.pdf/png` 使用同一批场景 8 数据。黑线是请求平均延迟，蓝线是控制器内有非零等待的键集数除以总键集数，两条线的误差棒均为样本标准差。CSV 同时保留主机排序时间和控制消息数的均值及方差。

## 控制器 SRPT 和 SBF 消融

上面的 TQS 命令也执行场景 8、`q=64` 的两种消融，BP-LPT 对照复用同一批次同种子的结果。

- **BP-LPT** 使用现有加权瓶颈排序。
- **控制器 SRPT** 按每个请求当前在控制器等待转发的键总数升序排列。
- **控制器 SBF** 按每个请求当前在控制器等待转发的最大单个键集大小升序排列。

SRPT 和 SBF 同值时按请求 ID 排序。所有模式共用原过载阈值、流表切换、转发定时及服务器服务逻辑。消融只使用控制器当前可见的待转发键集。查看 `ablation_summary.csv` 及 `controller_ablation.pdf/png` 获取三种排序的结果。`ablation_paired_deltas.csv` 提供每个种子的消融延迟减 BP-LPT 延迟，负值表示该种子下消融更快。

## 校验与代码入口

- `study_manifest.csv` 记录场景、算法、阈值、链路延时、运行编号、种子、排序策略和原始日志文件名。每份日志旁的 `.command` 和 `.exit` 记录实际命令、退出状态。
- `study_runs.csv` 保留逐次指标、完成请求数、有效状态及两类输入哈希。缺失或失败、未收到全部键集响应、或者配对输入不一致的运行会显示原因，并从均值和方差中排除。
- `scratch/sdn-kvs/sdn-kvs-main.cc` 读取随机种子和 `controllerRank`。`scratch/sdn-kvs/controller.cc` 在原发包计划之前选择请求次序。
- `export_latency.sh` 和 `export_tqs.sh` 通过 `run.sh` 运行仿真。`analyze_latency.sh` 和 `analyze_tqs.sh` 的 `--study` 模式调用 `analyze_sdn_kvs_study.py`，并由 `plot_sdn_kvs_study.py` 生成图表。

## 已有交接统计入口

需要单独复现场景 8–15 的交接与控制器开销时，可沿用已有统计模式。

```bash
bash export_tqs.sh --statistics results/handover-study
bash analyze_tqs.sh --statistics results/handover-study
```

场景 8 扫描全部十个静态阈值，其余场景用 64。该模式沿用原默认工作负载，每个基线运行一次，每个 SDN-KVS 条件运行三次。场景 14、15 使用 128 个客户端和 128 台服务器。交接逐轮记录位于各场景目录下的 `*_handover_rounds.csv`。汇总 `handover_idle_summary.csv` 对有效交接轮次求空闲时间总和。仿真时间分母为对应有效运行的仿真时钟总时长，主机调度耗时除以该分母可以超过 100%。
