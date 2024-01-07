import re
import matplotlib.pyplot as plt
pattern6 = re.compile(r"\[Controller\]\[\+(.*?)s\] Forward packet\(pkt_id=(\d+)\)\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), priority=(\d+)\) to the server (\d+)")
pattern5 = re.compile(r"\[Controller\]\[\+(.*?)s\] Schedule client packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), priority=(\d+)\)")
pattern3 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+)\(queue_siz=(\d+), update_time=\+(\d+[\.]{0,1}\d*)s\) received SDN-KVS packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), priority=(\d+)\) from (\d+\.\d+\.\d+\.\d+) port (\d+)")
pattern4 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+)\(queue_siz=(\d+), update_time=\+(\d+[\.]{0,1}\d*)s\) sent response packet\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), priority=(\d+)\) to (\d+\.\d+\.\d+\.\d+) port (\d+)")
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
with open('./output.log', 'r') as file:
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
plt.scatter(ks_server_wait_time_sorted.keys(), ks_server_wait_time_sorted.values(),color='green', alpha=0.5,label="wait in server")
plt.scatter(ks_ctrl_wait_time_sorted.keys(), ks_ctrl_wait_time_sorted.values(),color='blue', alpha=0.5,label="wait in ctrl")

# 添加标题和标签
plt.title('keyset wait time for {}'.format(algorithm))
plt.xlabel('keyset id')
plt.ylabel('wait_time(ms)')
plt.legend()
plt.savefig('wait_time.png')