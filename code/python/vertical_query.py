#!/usr/bin/env python3

import pyvisa

def query_vertical(channel):
    """查询指定通道的垂直档位（伏特为单位）"""
    try:
        rm = pyvisa.ResourceManager('@py')
        with rm.open_resource('USB0::0x1AB1::0x0515::MS5A244408466::INSTR') as instr:
            instr.timeout = 2000
            # 直接返回伏特值
            return float(instr.query(f":CHANnel{channel}:SCALe?"))
    except Exception as e:
        print(f"错误: {str(e)}")
        return None

if __name__ == "__main__":
    import sys
    if len(sys.argv) != 2:
        print("用法: python3 query_vertical.py <通道>")
        print("示例: python3 query_vertical.py 1")
        sys.exit(1)

    scale = query_vertical(int(sys.argv[1]))
    if scale is not None:
        print(scale)  # 直接打印伏特值，如 0.1 表示 0.1V/div
