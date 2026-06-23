#!/usr/bin/env python
# coding=utf-8
import re
import matplotlib
from matplotlib import font_manager
matplotlib.use("Agg")
matplotlib.rcParams['font.family'] = 'Times New Roman'
matplotlib.rcParams['font.weight'] = 'bold'
matplotlib.rcParams['axes.labelweight'] = 'bold'

import matplotlib.pyplot as plt
pattern5 = re.compile(r"\[Controller\]\[\+(.*?)s\] Schedule client packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\)\(ctrl_queue_siz=(\d+)\)")
pattern6 = re.compile(r"\[Controller\]\[\+(.*?)s\] Forward packet\(pkt_id=(\d+)\)\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\) to the server (\d+) \(ctrl_queue_siz=(\d+)\)")
pattern3 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+)\(queue_siz=(\d+), update_time=\+(\d+[\.]{0,1}\d*)s\) received SDN-KVS packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\) from (\d+\.\d+\.\d+\.\d+) port (\d+)")
pattern4 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+)\(queue_siz=(\d+), update_time=\+(\d+[\.]{0,1}\d*)s\) sent response packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), server_tqs=(\d+), priority=(\d+)\) to (\d+\.\d+\.\d+\.\d+) port (\d+)")
pattern9 = re.compile(r" - algorithm = ([A-Z-]+)\(server_algorithm: ([A-Z_]+), client_algorithm: ([A-Z]+), controller_algorithm: ([A-Z_]+)\)")

algorithm=""
ctrl_recv_time: dict[int, float] = {}
ctrl_send_time: dict[int, float] = {}
server_recv_time: dict[int, float] = {}
server_send_time: dict[int, float] = {}
server_service_time: dict[int, float] = {}

ks_ctrl_wait_time: dict[int, float] = {}
ks_server_wait_time: dict[int, float] = {}
ctrl_send_cnt=0
ctrl_recv_cnt=0
server_send_cnt=0
server_recv_cnt=0
with open('./results/output_c32_s32_k150000_r1:0:0_x0_y0_z0_u0.7_a4_q64_l600_h.log', 'r') as file:
  for line in file:
    match = pattern5.search(line)
    if match:
      time = float(match.group(1)) * 1000
      id = int(match.group(2))
      ctrl_recv_time[id] = time
      ctrl_recv_cnt += 1
    match = pattern6.search(line)
    if match:
      time = float(match.group(1)) * 1000
      id = int(match.group(3))
      ctrl_send_time[id] = time
      ctrl_send_cnt += 1
    match = pattern3.search(line)
    if match:
      time = float(match.group(1)) * 1000
      id = int(match.group(5))
      server_recv_time[id] = time
      server_recv_cnt += 1
    match = pattern4.search(line)
    if match:
      time = float(match.group(1)) * 1000
      id = int(match.group(5))
      server_send_time[id] = time
      server_service_time[id] = float(match.group(7))/float(match.group(10))
      server_send_cnt += 1
    match = pattern9.search(line)
    if match:
      algorithm = match.group(1)
for id in ctrl_send_time.keys():  
  ks_ctrl_wait_time[id] = ctrl_send_time[id] - ctrl_recv_time[id]
ks_ctrl_wait_time_sorted = {key: ks_ctrl_wait_time[key] for key in sorted(ks_ctrl_wait_time.keys())}

for id in server_recv_time.keys():
  ks_server_wait_time[id] = server_send_time[id]-server_recv_time[id]-server_service_time[id]

ks_server_wait_time_sorted = {key: ks_server_wait_time[key] for key in sorted(ks_server_wait_time.keys())}

# print("ctrl_send_cnt=",ctrl_send_cnt," ctrl_recv_cnt=",ctrl_recv_cnt)
# print(ks_ctrl_wait_time_sorted)
# print("server_send_cnt=",server_send_cnt," server_recv_cnt=",server_recv_cnt)
# print(ks_server_wait_time_sorted)


# 绘制散点图
plt.figure(figsize=(8, 4), dpi=300)

# 1. 绘制散点图（保持原有符号大小）
# Controller wait time
plt.scatter(ks_ctrl_wait_time_sorted.keys(), ks_ctrl_wait_time_sorted.values(), 
            color='#474646', s=20, alpha=0.95, label=None)
plt.scatter([], [], color='#474646', s=20, alpha=1.0, 
            label="Waiting on the controller")  # dummy for legend

# Server wait time
plt.scatter(ks_server_wait_time_sorted.keys(), ks_server_wait_time_sorted.values(), 
            color='#a1b6f1', s=15, alpha=0.1, marker='^', label=None)
plt.scatter([], [], color='#a1b6f1', s=15, alpha=1.0, marker='^', 
            label="Waiting on the server")  # dummy for legend

# 2. 设置坐标轴标签和刻度（增大字体并加粗）
plt.xlabel('Keyset Id', fontsize=20)  # 增大到24
plt.ylabel('Waiting Time (ms)', fontsize=20)  # 增大到24

# 3. 设置刻度标签
ax = plt.gca()
ax.tick_params(axis='both', which='major', labelsize=20)  # 刻度标签增大到20

# 4. 强制所有文本加粗并使用Times New Roman
for text in ax.get_xticklabels() + ax.get_yticklabels():
    text.set_fontweight('bold')
    text.set_fontname('Times New Roman')

# 5. 设置图例（增大字体）
legend = plt.legend(fontsize=20)
for text in legend.get_texts():
    text.set_fontweight('bold')
    text.set_fontname('Times New Roman')

# 其他设置保持不变
plt.yscale('function', functions=(lambda x: x**1.2, lambda x: x**(5/6)))
plt.xlim(0, 20000)
plt.ylim(0, 500)
plt.margins(0, 0)
plt.tight_layout()

plt.savefig("Fig7 WaitingTime.pdf", bbox_inches='tight')