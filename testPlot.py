import matplotlib.pyplot as plt

plt.rcParams['font.family'] = 'Times New Roman'


# 创建一个简单的在状留
plt.bar(['A','B','c'], [1,2,3])
                       
# 设置x釉标签
plt.xlabel('category', fontweight='bold', fontsize=24)

# 设置y铀标签
plt.ylabel('Value', fontweight='bold')
plt.show()
plt.savefig("testPlot.pdf", bbox_inches='tight')