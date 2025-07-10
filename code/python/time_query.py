import pyvisa

def get_timebase_scale(visa_address='USB0::0x1AB1::0x0515::MS5A244408466::INSTR'):
    """
    查询当前主时基档位，直接返回秒为单位的浮点数
    """
    rm = pyvisa.ResourceManager('@py')
    try:
        with rm.open_resource(visa_address) as instr:
            instr.timeout = 3000
            # 查询并直接返回秒为单位的浮点数
            return float(instr.query(":TIMebase:MAIN:SCALe?").strip())
    except Exception as e:
        print(f"查询失败: {str(e)}")
        return None

# 使用示例
if __name__ == "__main__":
    scale = get_timebase_scale()
    print( scale)
