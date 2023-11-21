awk '/Client/{print $0}' output.log > client_INFO.log
python ./analysis_data.py client_INFO.log