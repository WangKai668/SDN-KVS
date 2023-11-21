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

pattern = r"\[Client\]\[\+(.*?)s\].*?req_id=(\d+)"
request_sent_time = {}
request_receive_time={}
file_path = sys.argv[1]
if os.path.exists(file_path):
  with open(file_path, 'r') as file:
      for line in file:
        if 'Sent' in line:
          match = re.search(pattern, line)
          if match:
            time = match.group(1)
            request_id = match.group(2)
            request_sent_time[int(request_id)] = float(time)
        if 'Received' in line:
          match = re.search(pattern, line)
          if match:
            time = match.group(1)
            request_id = match.group(2)
            if request_id not in request_receive_time:
              request_receive_time[int(request_id)] = float(time)
            else:
              request_receive_time[int(request_id)] = float(time) if request_receive_time[request_id] < float(time) else request_receive_time[request_id]

  request_sent_time_sorted = {k: request_sent_time[k] for k in sorted(request_sent_time)}
  request_receive_time_sorted = {k: request_receive_time[k] for k in sorted(request_receive_time)}
  
  print(request_sent_time_sorted)
  print(request_receive_time_sorted)
else:
  print("file not exists!")