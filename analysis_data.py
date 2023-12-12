#!/usr/bin/env python
# coding=utf-8
#--------------#--------------#
# @Author: YJR-1100
# @Date: 2023-11-20 18:25:26
# @LastEditors: YJR-1100
# @LastEditTime: 2023-11-20 19:10:56
# @FilePath: \code\analysis_data.py
# @Description: 
# @
# @Copyright (c) 2023 by yjr-1100/CSU, All Rights Reserved. 
#--------------#--------------#
import re
import sys
import os.path
import numpy as np

pattern = r"\[Client\]\[\+(.*?)s\].*?req_id=(\d+)"
request_sent_time = {}
request_receive_time={}
file_path = sys.argv[1]
sent_cnt = 0
receive_cnt = 0
if os.path.exists(file_path):
  with open(file_path, 'r') as file:
      for line in file:
        if 'Sent' in line:
          sent_cnt += 1
          match = re.search(pattern, line)
          if match:
            time = match.group(1)
            request_id = match.group(2)
            request_sent_time[int(request_id)] = float(time)
        if 'Received' in line:
          receive_cnt += 1
          match = re.search(pattern, line)
          if match:
            time = match.group(1)
            request_id = match.group(2)
            if request_id not in request_receive_time:
              request_receive_time[int(request_id)] = float(time)
            else:
              request_receive_time[int(request_id)] = float(time) if request_receive_time[request_id] < float(time) else request_receive_time[request_id]

  if sent_cnt == receive_cnt:
    response_time=[]
    for k in request_receive_time:
      response_time.append(request_receive_time[k]-request_sent_time[k])
    response_time_sorted = sorted(response_time)
    p99 = np.percentile(response_time_sorted, 99)
    p95 = np.percentile(response_time_sorted, 95)
    print(sum(response_time_sorted)/len(response_time_sorted),p95,p99)
  else:
    print("error! There is packet loss")
    request_sent_time_sorted = {k: request_sent_time[k] for k in sorted(request_sent_time)}
    request_receive_time_sorted = {k: request_receive_time[k] for k in sorted(request_receive_time)}
    print(len(request_sent_time_sorted),request_sent_time_sorted)
    print(len(request_receive_time_sorted),request_receive_time_sorted)
else:
  print("file not exists!")