#!/usr/bin/env python3

import pyvisa
import sys

def set_vertical(scale, probe_atten=1, unit='V'):
    """
    设置垂直档位（通道1固定）
    :param scale: 档位值 (如0.1)
    :param probe_atten: 探头衰减比 (如10)
    :param unit: 单位 ('V'或'mV')
    """
    try:
        # 固定通道号为1
        channel = 1
        
        rm = pyvisa.ResourceManager('@py')
        with rm.open_resource('USB0::0x1AB1::0x0515::MS5A244408466::INSTR') as instr:
            instr.timeout = 2000

            # 设置探头衰减比
            instr.write(f":CHANnel{channel}:PROBe {probe_atten}")

            # 计算实际电压值（考虑探头衰减）
            actual_scale = scale * probe_atten
            if unit == 'mV':
                actual_scale /= 1000

            # 设置垂直档位
            instr.write(f":CHANnel{channel}:SCALe {actual_scale}")

            # 验证设置
            actual = float(instr.query(f":CHANnel{channel}:SCALe?"))
            probe_set = float(instr.query(f":CHANnel{channel}:PROBe?"))

            print(f"通道{channel}设置完成 → {actual*1000 if unit=='mV' else actual:.3f} {unit}/div ")

    except Exception as e:
        print(f"错误: {str(e)}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python3 set_vertical.py <档位> [单位=V/mV] [探头衰减=1]")
        print("示例1: python3 set_vertical.py 0.1 V 10")
        print("示例2: python3 set_vertical.py 100 mV")
        sys.exit(1)

    scale = float(sys.argv[1])
    unit = sys.argv[2] if len(sys.argv) > 2 else 'V'
    probe = float(sys.argv[3]) if len(sys.argv) > 3 else 1

    set_vertical(scale, probe, unit)
