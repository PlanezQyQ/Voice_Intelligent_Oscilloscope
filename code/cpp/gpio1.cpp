//gpio include
#include <gpiod.h>
#include <unistd.h>
#include <cstdio>
#include <csignal>
#include <vector>
#include <cstring>
#include <cerrno>

//rigol include
 //#include <string>
 #include <cstdlib>  
 #include <regex>
 //#include <iostream>
 //#include <cstdio>
 #include <array>
 #include <stdexcept>
 //#include <vector>
 #include <filesystem>

//deepseek include
#include <string.h>
#include <unistd.h>
#include <string>
#include "rkllm.h"
#include <fstream>
#include <iostream>
#include <csignal>
//#include <vector>

using namespace std;

//gpio function
volatile sig_atomic_t stop = 0;

void signalHandler(int signum) {
    stop = 1;
}

//deepseek function

LLMHandle llmHandle = nullptr;
std::string input_head;
std::string output_str;
bool Flag_StartLLM = 0;

//scpi function
bool Flag_CtrlOSC = 0;

void exit_handler(int signal);
int callback(RKLLMResult *result, void *userdata, LLMCallState state);

//under is python turn c++
namespace fs = std::filesystem;

std::string exec_python_script(const std::string& python_path, 
                             const std::string& script_path, 
                             const std::string& args = "") {
    std::string command = python_path + " " + script_path;
    if (!args.empty()) command += " " + args;
    
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        std::cerr << "[Error] Failed to execute: " << command << std::endl;
        return "";
    }
    
    char buffer[128];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    
    if (pclose(pipe) != 0) {
        std::cerr << "[Error] Script failed: " << command << std::endl;
        return "";
    }
    return result;
}
//up is python turn c++

// check out_AI and modify flag0
void checkout_AI(const std::string& out_AI, int& flag0) 
{
    // 1. 提取 </think> 之后的所有内容
    std::string new_str;
    size_t think_pos = out_AI.find("</think>");
    if (think_pos != std::string::npos) {
        new_str = out_AI.substr(think_pos + 5); // 5是"</think>"的长度
    } else {
        new_str = out_AI; // 如果没有找到</think>，使用原始字符串
    }

    // 2. 定义匹配模式（简化版）
    std::regex vertical_after_think(R"(CHANnel1)", std::regex::icase);
    std::regex time_after_think(R"(TIMebase)", std::regex::icase);
    std::regex auto_after_think(R"(AUToscale)", std::regex::icase);
    
    std::smatch matches;

    // 3. 在提取的新字符串中进行匹配
    if (std::regex_search(new_str, matches, auto_after_think)) {
        flag0 = 3; 
        return;
    } 
    else if (std::regex_search(new_str, matches, time_after_think)) {
        flag0 = 2;
        return;
    } 
    else if (std::regex_search(new_str, matches, vertical_after_think)) {
        flag0 = 1; 
        return;
    } 
    else {
        flag0 = 0;  // 无匹配
    }
}
//catch number from outAI
std::vector<double> extract_scale_values(const std::string& out_AI) {
    std::vector<double> results;
    // 1. 提取 </think> 之后的所有内容
    std::string new_str;
    size_t think_pos = out_AI.find("</think>");
    if (think_pos != std::string::npos) {
        new_str = out_AI.substr(think_pos + 5); // 5是"</think>"的长度
    } else {
        new_str = out_AI; // 如果没有找到</think>，使用原始字符串
    }
    
    // 使用更严格的正则表达式
    std::regex pattern(R"(SCALe\s+([-+]?\d+\.?\d*))", 
                      std::regex::icase | std::regex_constants::ECMAScript);
    
    std::sregex_iterator it(new_str.begin(), new_str.end(), pattern);
    std::sregex_iterator end;
    
    for (; it != end; ++it) {
        try {
            results.push_back(std::stod((*it)[1].str()));
        } catch (...) {
            continue;
        }
    }
    
    return results;
}

int main(int argc , char **argv) {
	std::string time_value ;

    std::string vertical_value ;
	
    signal(SIGINT, signalHandler);
    
    // 配置不同芯片上的GPIO引脚
    struct GPIOPin {
        const char* chipname;
        unsigned int pin;
        gpiod_chip* chip;
        gpiod_line* line;
    };
    
    std::vector<GPIOPin> pins = {
        {"gpiochip0", 20, nullptr, nullptr},
        {"gpiochip0", 21, nullptr, nullptr},
        {"gpiochip4", 11, nullptr, nullptr}  // 注意：这里修改为11而不是139
    };
    
    // 打开所有GPIO芯片并获取引脚
    for (auto& pin : pins) {
        pin.chip = gpiod_chip_open_by_name(pin.chipname);
        if (!pin.chip) {
            fprintf(stderr, "错误: 无法打开芯片 %s (%s)\n", 
                    pin.chipname, strerror(errno));
            // 尝试关闭已打开的芯片
            for (auto& p : pins) {
                if (p.chip) gpiod_chip_close(p.chip);
            }
            return 1;
        }
        
        printf("成功打开芯片: %s (标签: %s)\n", 
               pin.chipname, gpiod_chip_label(pin.chip));
        
        pin.line = gpiod_chip_get_line(pin.chip, pin.pin);
        if (!pin.line) {
            fprintf(stderr, "错误: 无法获取芯片 %s 上的引脚 %d (%s)\n", 
                    pin.chipname, pin.pin, strerror(errno));
            // 尝试关闭已打开的芯片
            for (auto& p : pins) {
                if (p.chip) gpiod_chip_close(p.chip);
            }
            return 1;
        }
        
        printf("成功获取芯片 %s 上的引脚 %d\n", pin.chipname, pin.pin);
        
        // 配置为输入模式
        if (gpiod_line_request_input(pin.line, "multi_chip_monitor") < 0) {
            fprintf(stderr, "错误: 无法配置芯片 %s 上的引脚 %d 为输入 (%s)\n", 
                    pin.chipname, pin.pin, strerror(errno));
            // 尝试关闭已打开的芯片
            for (auto& p : pins) {
                if (p.chip) gpiod_chip_close(p.chip);
            }
            return 1;
        }
        
        printf("成功配置芯片 %s 上的引脚 %d 为输入模式\n", pin.chipname, pin.pin);
    }
    
    printf("\n开始监控多芯片GPIO...\n");
    printf("芯片0: 引脚20 (GPIO20), 引脚21 (GPIO21)\n");
    printf("芯片4: 引脚11 (GPIO11)\n");  // 修改为11
    printf("等待组合: \n");
    printf("  1) [1][0][0] -> a=1\n");
    printf("  2) [1][1][1] -> a=2\n");
    printf("  3) [0][0][0] -> a=3\n");
    printf("按 Ctrl+C 退出程序\n\n");

    // ================================================================================================================
	// 初始化模型

	if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " model_path max_new_tokens max_context_len\n";
        return 1;
    }

    signal(SIGINT, exit_handler);
    printf("rkllm init start\n");

    //设置参数及初始化
    RKLLMParam param = rkllm_createDefaultParam();
    param.model_path = argv[1];

    //设置采样参数
    param.top_k = 1;
    param.top_p = 0.95;
    param.temperature = 0.8;
    param.repeat_penalty = 1.1;
    param.frequency_penalty = 0.0;
    param.presence_penalty = 0.0;

    param.max_new_tokens = std::atoi(argv[2]);
    param.max_context_len = std::atoi(argv[3]);
    param.skip_special_token = true;
    param.extend_param.base_domain_id = 0;
    param.extend_param.embed_flash = 1;

    int ret = rkllm_init(&llmHandle, &param, callback);
    if (ret == 0){
        printf("rkllm init success\n");
    } else {
        printf("rkllm init failed\n");
        exit_handler(-1);
    }

    vector<string> pre_input;
    pre_input.push_back("现有一笼子，里面有鸡和兔子若干只，数一数，共有头14个，腿38条，求鸡和兔子各有多少只？");
    pre_input.push_back("有28位小朋友排成一行,从左边开始数第10位是学豆,从右边开始数他是第几位?");
    cout << "\n**********************可输入以下问题对应序号获取回答/或自定义输入********************\n"
         << endl;
    for (int i = 0; i < (int)pre_input.size(); i++)
    {
        cout << "[" << i << "] " << pre_input[i] << endl;
    }
    cout << "\n*************************************************************************\n"
         << endl;

    RKLLMInput rkllm_input;
    memset(&rkllm_input, 0, sizeof(RKLLMInput));  // 将所有内容初始化为 0
    
    // 初始化 infer 参数结构体
    RKLLMInferParam rkllm_infer_params;
    memset(&rkllm_infer_params, 0, sizeof(RKLLMInferParam));  // 将所有内容初始化为 0

    // 1. 初始化并设置 LoRA 参数（如果需要使用 LoRA）
    // RKLLMLoraAdapter lora_adapter;
    // memset(&lora_adapter, 0, sizeof(RKLLMLoraAdapter));
    // lora_adapter.lora_adapter_path = "qwen0.5b_fp16_lora.rkllm";
    // lora_adapter.lora_adapter_name = "test";
    // lora_adapter.scale = 1.0;
    // ret = rkllm_load_lora(llmHandle, &lora_adapter);
    // if (ret != 0) {
    //     printf("\nload lora failed\n");
    // }

    // 加载第二个lora
    // lora_adapter.lora_adapter_path = "Qwen2-0.5B-Instruct-all-rank8-F16-LoRA.gguf";
    // lora_adapter.lora_adapter_name = "knowledge_old";
    // lora_adapter.scale = 1.0;
    // ret = rkllm_load_lora(llmHandle, &lora_adapter);
    // if (ret != 0) {
    //     printf("\nload lora failed\n");
    // }

    // RKLLMLoraParam lora_params;
    // lora_params.lora_adapter_name = "test";  // 指定用于推理的 lora 名称
    // rkllm_infer_params.lora_params = &lora_params;

    // 2. 初始化并设置 Prompt Cache 参数（如果需要使用 prompt cache）
    // RKLLMPromptCacheParam prompt_cache_params;
    // prompt_cache_params.save_prompt_cache = true;                  // 是否保存 prompt cache
    // prompt_cache_params.prompt_cache_path = "./prompt_cache.bin";  // 若需要保存prompt cache, 指定 cache 文件路径
    // rkllm_infer_params.prompt_cache_params = &prompt_cache_params;
    
    // rkllm_load_prompt_cache(llmHandle, "./prompt_cache.bin"); // 加载缓存的cache

    rkllm_infer_params.mode = RKLLM_INFER_GENERATE;
    // By default, the chat operates in single-turn mode (no context retention)
    // 0 means no history is retained, each query is independent
    rkllm_infer_params.keep_history = 0;

    //The model has a built-in chat template by default, which defines how prompts are formatted  
    //for conversation. Users can modify this template using this function to customize the  
    //system prompt, prefix, and postfix according to their needs.  
    // rkllm_set_chat_template(llmHandle, "", "<｜User｜>", "<｜Assistant｜>");

	//=================================================================================================================
    std::string last_gpio_order = "";  // 初始化为空字符串，用于存储上次检测到的有效状态
    bool detection_enabled = false;    // 是否启用检测（初始为false）
while (!stop) {
    // 读取所有引脚值
    std::vector<int> values;
    bool valid = true;
    
    for (auto& pin : pins) {
        int val = gpiod_line_get_value(pin.line);
        if (val < 0) {
            fprintf(stderr, "警告: 读取芯片 %s 上的引脚 %d 失败 (%s)\n", 
                    pin.chipname, pin.pin, strerror(errno));
            valid = false;
            break;
        }
        values.push_back(val);
    }
    
    if (valid && values.size() == 3) 
    {
        if (!detection_enabled) 
        {
            // 修改：检查是否为000组合
            if (values[0] == 0 && values[1] == 0 && values[2] == 0) {
                detection_enabled = true;  // 启用检测
                // 修改：添加启用提示
                printf("检测到初始化组合 [000]，开始监控GPIO状态...\n");
            }
            // 修改：跳过后续处理直到启用检测
            continue;
        }
        // 根据新的引脚组合规则设置状态字符串
        if (values[0] == 0 && values[1] == 0 && values[2] == 1) {
            input_head = "你是一个示波器程控专家，需将用户指令转化为SCPI代码，你必须严格遵守我的要求输出正确回答，否则我会狠狠地踢我旁边的小猫。规则如下： 1. **指令映射**： - 【自动调节】→ `:AUToscale`（当用户指令为'自动调节'时使用） 2. **输出规范**- 严禁输出解释性语句，严禁任何解释、说明、前缀后缀，仅返回SCPI命令或错误码 3.用户指令：`自动调节` 4. **当前状态**（需主控程序实时填充）：";
        } else if (values[0] == 0 && values[1] == 1 && values[2] == 0) {
            input_head = "你是一个示波器程控专家，需将用户指令转化为SCPI代码，你必须严格遵守我的要求输出正确回答，否则我会狠狠地踢我旁边的小猫。规则如下： 1. **指令映射**： - 【时基调节】→ `:TIMebase:MAIN:SCALe <值>`(当用户指令包含‘时基’时使用) • 调大：在有效参数列表中选择所有大于当前时基的枚举值中最小的一个输出（如0.0001 → 0.0002） 2. **参数枚举值**（禁止超出范围,每次输出指令只取一个值）： | **指令类型** | **有效参数列表** | |--------------|------------------| | 时基值 |  0.00001, 0.00002, 0.00005, 0.0001, 0.0002, 0.0005, 0.001 | 3. **输出规范**- 严禁输出解释性语句，严禁任何解释、说明、前缀后缀，仅返回SCPI命令或错误码,必须确保输出值大于当前时基,且在所有符合大小的可能输出中最小且不等于当前状态 4.用户指令：`调大时基` 5. **当前状态**（需主控程序实时填充）：";
        } else if (values[0] == 0 && values[1] == 1 && values[2] == 1) {
            input_head = "你是一个示波器程控专家，需将用户指令转化为SCPI代码，你必须严格遵守我的要求输出正确回答，否则我会狠狠地踢我旁边的小猫。规则如下： 1. **指令映射**： - 【时基调节】→ `:TIMebase:MAIN:SCALe <值>`(当用户指令包含‘时基’时使用) • 调小：在有效参数列表中选择所有小于当前时基的枚举值中最大的一个输出（如 0.0002 → 0.0001） 2. **参数枚举值**（禁止超出范围,每次输出指令只取一个值）： | **指令类型** | **有效参数列表** | |--------------|------------------| | 时基值 |  0.001, 0.0005, 0.0002, 0.0001, 0.00005, 0.00002, 0.00001 | 3. **输出规范**- 严禁输出解释性语句，严禁任何解释、说明、前缀后缀，仅返回SCPI命令或错误码,必须确保输出值小于当前时基,且在所有符合大小的可能输出中最大且不等于当前状态 4.用户指令：`调小时基` 5. **当前状态**（需主控程序实时填充）：";
        } else if (values[0] == 1 && values[1] == 0 && values[2] == 0) {
            input_head = "你是一个示波器程控专家，需将用户指令转化为SCPI代码，你必须严格遵守我的要求输出正确回答，否则我会狠狠地踢我旁边的小猫。规则如下： 1. **指令映射**： - 【垂直档位】→ `:CHANnel1:SCALe <值>`(当用户指令包含‘垂直档位’时使用) • 调大：在有效参数列表中选择所有大于当前垂直档位的枚举值中最小的一个输出（如0.2 → 0.5） 2. **参数枚举值**（禁止超出范围,每次输出指令只取一个值）： | **指令类型** | **有效参数列表** | |--------------|------------------|  | 垂直档位值 | 0.05, 0.1, 0.2, 0.5, 1, 2, 5 | 3. **输出规范**- 严禁输出解释性语句，严禁任何解释、说明、前缀后缀，仅返回SCPI命令或错误码,必须确保输出值大于当前垂直档位,且在所有符合大小的可能输出中最小且不等于当前状态 4.用户指令：`调大垂直档位` 5. **当前状态**（需主控程序实时填充）：";
        } else if (values[0] == 1 && values[1] == 1 && values[2] == 0) {
            input_head = "你是一个示波器程控专家，需将用户指令转化为SCPI代码，你必须严格遵守我的要求输出正确回答，否则我会狠狠地踢我旁边的小猫。规则如下： 1. **指令映射**： - 【垂直档位】→ `:CHANnel1:SCALe <值>`(当用户指令包含‘垂直档位’时使用) • 调小：在有效参数列表中选择所有小于当前垂直档位的枚举值中最大的一个输出（如0.5 → 0.2） 2. **参数枚举值**（禁止超出范围,每次输出指令只取一个值）： | **指令类型** | **有效参数列表** | |--------------|------------------|  | 垂直档位值 | 5, 2, 1, 0.5, 0.2, 0.1, 0.05 | 3. **输出规范**- 严禁输出解释性语句，严禁任何解释、说明、前缀后缀，仅返回SCPI命令或错误码,必须确保输出值小于当前垂直档位,且在所有符合大小的可能输出中最大且不等于当前状态 4.用户指令：`调小垂直档位` 5. **当前状态**（需主控程序实时填充）：";
        } else {
            input_head = "";
        }
        // 其他组合保持空字符串
        
        // 只在状态变化时输出
        if (last_gpio_order.compare(input_head) != 0) 
        {
            last_gpio_order = input_head;  // 更新最后检测到的状态
            printf("GPIO状态: [%d][%d][%d] ", values[0], values[1], values[2]);
            
            if (input_head.empty()) {
                printf("无匹配组合！\n");
            } else {
                printf("==> 检测到组合! 操作: %s\n", input_head.c_str());
                Flag_StartLLM = 1;  // 触发标志保持不变
            }
        }
    }   // 结束 if (valid && values.size() == 3)
        
        if(Flag_StartLLM)
        {
            // set parameter===========================================
            // 1=time_query, 2=time_set, 3=vertical_query, 4=vertical_set
            const std::string python_path = "/usr/bin/python3";
            const std::string script_dir = "/root/";
            	std::string  args;	
            	std::string  arg="1";
            // python roads
            const std::string time_query_path = script_dir + "time_query.py";
           
            const std::string vertical_query_path = script_dir + "vertical_query.py";
          
	        // ===========================================

            // run python nano and save result to back_rigol
            std::string back_rigol_time = exec_python_script(python_path, time_query_path, args);
            std::string back_rigol_vertical = exec_python_script(python_path, vertical_query_path, arg);

            // 1. 将字符串转换为 double
            double time_valu = strtod(back_rigol_time.c_str(), nullptr);

            // 2. 使用 stringstream 格式化为固定小数
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(8) << time_valu;  // 8位小数
            back_rigol_time = oss.str();  // 现在 back_rigol_time = "0.00020000"
			
			// ================================================================================================================
			// 启动模型推理
			// 清空模型输出
			output_str.clear(); 
        	printf("\n");
        	printf("user: ");
			//生成模型输入
			std::string input_str;
			input_str = input_head + " • 当前时基 = " + back_rigol_time + " • 当前垂直档位 = " + back_rigol_vertical;
			//input_str = input_head + " • 当前时基 = " + "0.001" + " • 当前垂直档位 = " + "0.2" + " 用户指令：" + gpio_order;
			std::cout << input_str << std::endl;
        	if (input_str == "exit")
        	{
            break;
        	}
        	if (input_str == "clear")
        	{
        	    ret = rkllm_clear_kv_cache(llmHandle, 1, nullptr, nullptr);
       		    if (ret != 0)
            	{
            	    printf("clear kv cache failed!\n");
            	}
        	    continue;
        	}
        	for (int i = 0; i < (int)pre_input.size(); i++)
        	{
        	    if (input_str == to_string(i))
        	    {
        	        input_str = pre_input[i];
        	        cout << input_str << endl;
        	    }
        	}
        	rkllm_input.input_type = RKLLM_INPUT_PROMPT;
        	rkllm_input.role = "user";
        	rkllm_input.prompt_input = (char *)input_str.c_str();
        	printf("robot: ");

        	// 若要使用普通推理功能,则配置rkllm_infer_mode为RKLLM_INFER_GENERATE或不配置参数
        	rkllm_run(llmHandle, &rkllm_input, &rkllm_infer_params, NULL);

            //cout << "output_str:" << output_str << std::endl;
			
			//===============================================================================================================

            Flag_StartLLM = 0;
            Flag_CtrlOSC = 1;
        }

        if(Flag_CtrlOSC)
        {	 // set parameter===========================================
            // 1=time_query, 2=time_set, 3=vertical_query, 4=vertical_set
            const std::string python_path = "/usr/bin/python3";
            const std::string script_dir = "/root/";
            const std::string  time_unit = "s";
            const std::string  vertical_unit = "V";
    		
    				
            // python roads
            const std::string time_query_path = script_dir + "time_query.py";
            const std::string time_set_path = script_dir + "time_set.py";
            const std::string vertical_query_path = script_dir + "vertical_query.py";
            const std::string vertical_set_path = script_dir + "vertical_set.py";
            const std::string myauto_path = script_dir + "myauto.py";
	        // ===========================================

            // have out_AI
            int flag0 = 0;//flag0 choose veritical or time or base
            int flag = 0 ;//flag  choose query or set
			cout << flag;
			
	        checkout_AI(output_str,flag0);
	        if (flag0 == 1) 
			{  // vertical
    			flag = 4;
    			auto values = extract_scale_values(output_str);
    			if (!values.empty()) 
					{
        			vertical_value = std::to_string(values[0]);  // 存储为字符串
    				}
			}
			if (flag0 == 2) {  // time
   				 flag = 2;
    			auto values = extract_scale_values(output_str);
    			if (!values.empty()) 
					{
        			time_value = std::to_string(values[0]);      // 存储为字符串
    				}
				}
	        if (flag0==3)  {flag=5;
	        cout << flag;
			}
    
           	cout << flag;
            // becase of flag choose diferent python nano
            std::string target_script, args;
            //std::string passage = "1";
            switch(flag) {
            	
                case 0: 
                     std::cout << "flag is error" << std::endl;
                     cout << flag;
                    break;
				case 1:  
                    target_script = time_query_path;
                    std::cout << "time_query_path" << std::endl;
                    break;
                case 2: 
                    target_script = time_set_path; args = time_value + " " + time_unit;
                    std::cout << "time_set_path" << std::endl;
                    break;
                case 3: 
                    target_script = vertical_query_path;
					std::cout << "vertical_query_path" << std::endl; 
                    break;
                case 4: 
                    //target_script = vertical_set_path; args = passage + " " + vertical_value + " " + vertical_unit;
                    target_script = vertical_set_path; args = vertical_value + " " + vertical_unit;
                    std::cout << "vertical_set_path" << std::endl;
                    break;
                case 5: 
                    target_script = myauto_path;
					std::cout << "myauto_path" << std::endl; 
                    break;    
                
                }

            // run python nano and save result to back_rigol
            std::string back_rigol = exec_python_script(python_path, target_script, args);
    
            if (back_rigol.empty()) {
                std::cerr << "[Error] No output from script" << std::endl;
                return 1;
            }
            std::cout << back_rigol << std::endl;
            Flag_CtrlOSC = 0;
        }

        usleep(50); // 1ms延迟
    }  // 结束 while 循环
    
    // 清理资源
    printf("\n释放资源...\n");
    for (auto& pin : pins) {
        if (pin.line) {
            gpiod_line_release(pin.line);
            printf("释放芯片 %s 上的引脚 %d\n", pin.chipname, pin.pin);
        }
        if (pin.chip) {
            gpiod_chip_close(pin.chip);
            printf("关闭芯片 %s\n", pin.chipname);
        }
    }

    rkllm_destroy(llmHandle);

    printf("程序已安全退出\n");
    
    return 0;
}

//deepseek function definition

void exit_handler(int signal)
{
    if (llmHandle != nullptr)
    {
        {
            cout << "程序即将退出" << endl;
            LLMHandle _tmp = llmHandle;
            llmHandle = nullptr;
            rkllm_destroy(_tmp);
        }
    }
    exit(signal);
}

int callback(RKLLMResult *result, void *userdata, LLMCallState state)
{
    if (state == RKLLM_RUN_NORMAL) {
        // 累积文本片段
        output_str += result->text; 
        printf("%s", result->text); // 保留实时打印
    }
    else if (state == RKLLM_RUN_FINISH) {
        printf("\n[完整结果已存储至output_str]\n");
    }
    return 0;
}
