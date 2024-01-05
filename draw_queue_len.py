import re
import matplotlib.pyplot as plt
# pattern6 = re.compile(r"\[Controller\]\[\+(.*?)s\] Forward packet\(pkt_id=(\d+)\)\(id=(\d+), req_id=(\d+), ks_siz=(\d+), bottleneck_ks_siz=(\d+), queue_siz=(\d+), server_rate=(\d+), heavy=(\d+), priority=(\d+)\) to the server (\d+)")
pattern5 = re.compile(r"\[Controller\]\[\+(.*?)s\] Do BP-LPT for array\(size=(.*?), tot_key_num=(.*?)\): \[")
pattern3 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+)\(queue_siz=(\d+), update_time=\+(\d+[\.]{0,1}\d*)s\) will finish handled in \+(.*?)s")
pattern4 = re.compile(r"\[Server\]\[\+(.*?)s\] Server (\d+)\(queue_siz=(\d+), update_time=\+(\d+[\.]{0,1}\d*)s\) sent response packet")
pattern9 = re.compile(r" - algorithm = ([A-Z-]+)\(server_algorithm: ([A-Z]+), client_algorithm: ([A-Z]+), controller_algorithm: ([A-Z_]+)\)")

algorithm=""
ctrl_queue_size={}
all_server_queue_size_receive={}
all_server_queue_size_sent={}
with open('./output.log', 'r') as file:
  for line in file:
    match = pattern5.search(line)
    if match:
      time = float(match.group(1)) * 1000
      ctrl_queue_size[time]=int(match.group(3))
    match = pattern3.search(line)
    if match:
      time = float(match.group(1)) * 1000
      server_id = int(match.group(2))
      queue_siz = int(match.group(3))
      if server_id not in all_server_queue_size_receive.keys():
        all_server_queue_size_receive[server_id]={}
      all_server_queue_size_receive[server_id][time]=queue_siz
    match = pattern4.search(line)
    if match:
      time = float(match.group(1)) * 1000
      server_id = int(match.group(2))
      queue_siz = int(match.group(3))
      if server_id not in all_server_queue_size_sent.keys():
        all_server_queue_size_sent[server_id]={}
      all_server_queue_size_sent[server_id][time]=queue_siz
    match = pattern9.search(line)
    if match:
      algorithm = match.group(1)
      

for server_id in all_server_queue_size_receive.keys():
  plt.plot(all_server_queue_size_receive[server_id].keys(), all_server_queue_size_receive[server_id].values(),alpha=0.5,label="server {}".format(server_id))


plt.plot(ctrl_queue_size.keys(),ctrl_queue_size.values(),label="ctrl")
# # 添加标题和标签
plt.title('queue size in server for {}'.format(algorithm))
plt.xlabel('time(ms)')
plt.ylabel('queue size')
plt.legend()
plt.savefig('queue_len_receive.png')


plt.clf()
for server_id in all_server_queue_size_sent.keys():
  plt.plot(all_server_queue_size_sent[server_id].keys(), all_server_queue_size_sent[server_id].values(),alpha=0.5,label="server {}".format(server_id))


plt.plot(ctrl_queue_size.keys(),ctrl_queue_size.values(),label="ctrl")
# # 添加标题和标签
plt.title('queue size in server for {}'.format(algorithm))
plt.xlabel('time(ms)')
plt.ylabel('queue size')
plt.legend()
plt.savefig('queue_len_sent.png')