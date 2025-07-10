#!/usr/bin/env python3
import pyvisa
import sys

# 参数检查
if len(sys.argv) != 3:
    print("用法: python3 rigol_scale.py <数值> <单位>")
    print("示例: python3 rigol_scale.py 200 us")
    sys.exit(1)

# 获取输入参数
value = float(sys.argv[1])
unit = sys.argv[2].lower()

try:
    # 连接设备
    rm = pyvisa.ResourceManager('@py')
    scope = rm.open_resource('USB0::0x1AB1::0x0515::MS5A244408466::INSTR')
    scope.timeout = 2000  # 2秒超时

    # 单位转换
    units = {'s':1, 'ms':1e-3, 'us':1e-6, 'ns':1e-9}
    scale = value * units[unit]

    # 设置时基
    scope.write(f":TIMebase:MAIN:SCALe {scale}")
    
    # 读取实际值
    actual = float(scope.query(":TIMebase:MAIN:SCALe?"))
    print(f"实际设置: {actual/units[unit]:g} {unit}/div")

except Exception as e:
    print(f"错误: {e}")
finally:
    if 'scope' in locals():
        scope.close()
