#!/usr/bin/env python3
import pyvisa
import time
import sys

VISA_ADDRESS = 'USB0::0x1AB1::0x0515::MS5A244408466::INSTR'

class RigolScopeController:
    def __init__(self):
        self.rm = pyvisa.ResourceManager('@py')
        self.scope = None

    def connect(self):
        try:
            self.scope = self.rm.open_resource(VISA_ADDRESS)
            self.scope.timeout = 3000
            print(f"Connected: {self.scope.query('*IDN?').strip()}")
            return True
        except Exception as e:
            print(f"Connection failed: {str(e)}")
            return False

    def auto_scale(self):
        """Execute auto scaling"""
        try:
            self.scope.write(':CHANnel1:DISPlay ON')
            self.scope.write(':AUToscale')
            time.sleep(3)
            
            v_scale = self.scope.query(':CHANnel1:SCALe?').strip()
            t_scale = self.scope.query(':TIMebase:MAIN:SCALe?').strip()
            print(f"Auto scale completed\nVertical: {v_scale} V/div\nTimebase: {t_scale} s/div")
            return True
        except Exception as e:
            print(f"Auto scale failed: {str(e)}")
            return False

    def manual_set(self, v_scale=0.1, t_scale=0.001):
        """Manual settings"""
        try:
            self.scope.write(f':CHANnel1:SCALe {v_scale}')
            self.scope.write(f':TIMebase:MAIN:SCALe {t_scale}')
            
            actual_v = self.scope.query(':CHANnel1:SCALe?').strip()
            actual_t = self.scope.query(':TIMebase:MAIN:SCALe?').strip()
            print(f"Manual settings applied\nVertical: {actual_v} V/div\nTimebase: {actual_t} s/div")
            return True
        except Exception as e:
            print(f"Manual setup failed: {str(e)}")
            return False

    def close(self):
        if self.scope:
            self.scope.close()

if __name__ == "__main__":
    controller = RigolScopeController()
    if controller.connect():
        if len(sys.argv) > 1 and sys.argv[1] == 'manual':
            controller.manual_set(v_scale=0.5, t_scale=0.002)
        else:
            controller.auto_scale()
        controller.close()
