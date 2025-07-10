#!/bin/bash

# 运行模型推理
./gpio1 DeepSeek-R1-Distill-Qwen-1.5B_W8A8_RK3588.rkllm 4096 4096 || {
    echo "错误：模型执行失败！"
    exit 1
}
