#include <mutex>
#include <csignal>
#include <fstream>
#include "TiRPC.hpp"

bool g_loop = true;

void SignalHandler(int signum)
{
    switch (signum) {
    case SIGINT:
        g_loop = false;
        break;
    default:
        break;
    }
}


class Order {
public:
    void Outp(const std::string& log)
    {
        std::lock_guard<std::mutex> locker(mutex);
        logs.emplace_back(log);
    }

    void SwapLogs(std::vector<std::string>& out)
    {
        std::lock_guard<std::mutex> locker(mutex);
        out.swap(logs);
    }

private:
    std::mutex mutex; // for logs
    std::vector<std::string> logs;
} g_order;

int main(int argc, char* argv[])
{
    std::map<std::string, std::string> configs;
    configs["ip"] = "127.0.0.1";
    configs["p1"] = "6021";
    configs["p2"] = "6022";
    configs["workers"] = "1";  // default worker count: 1
    configs["timeout"] = "300"; // default timeout: 300s
    configs["command"] = "";
    configs["output"] = "output.log";
    configs["group"] = ":";

    std::vector<std::string> commands;
    std::vector<std::string> groups;
    std::vector<std::string> envs;
    
    for (int argn = 1; argn < argc; argn++) {
        std::string config = argv[argn];
        size_t split = config.find_first_of('=');
        std::string key = config.substr(0, split);
        std::string value = config.substr(split + 1);
        configs[key] = value;
        if(key == "command"){
            if(commands.size() > groups.size()) {
                groups.push_back(" ");
            }
            if(commands.size() > envs.size()) {
                envs.push_back("");
            }
            commands.push_back(configs[key]);
        }
        else if(key == "group"){
            groups.push_back(configs[key]);
        }
        else if(key == "env"){
            envs.push_back(configs[key]);
        }
    }

    if(configs["command"] == "") {
        std::cout<<"Need command!" << std::endl;
        return -1;
    }
    
    if(configs["output"].find(".log") == configs["output"].npos)
    {
        if(configs["output"][configs["output"].length() - 1] != '/')
        {
            configs["output"] += "/";
        }
        size_t head = configs["command"].rfind("/");
        if(head == configs["command"].npos)
        {
            head = 0;
        }
        else
        {
            head++;
        }
        size_t tail = configs["command"].rfind(".ini");
        configs["output"] = configs["output"] + \
        configs["command"].substr(head, tail - head) + ".log";
    }

    int timeout = atoi(configs["timeout"].c_str());
    int workers = atoi(configs["workers"].c_str());
    int worker_count = 0;

    std::ofstream ofs(configs["output"]); // text file (default)
    if (!ofs.is_open()) {
        std::cout<<"Error: Open file failed."<<std::endl;
        return -2;
    }

    auto start = std::chrono::system_clock::now();

    signal(SIGINT, SignalHandler);

    tirpc::RpcAsyncBroadcast rpc;
    
    rpc.BindFunc("Outp", &Order::Outp, g_order);
    if (!rpc.Start(tirpc::RpcAsyncBroadcast::Role::Client, configs["ip"],
        atoi(configs["p1"].c_str()), atoi(configs["p2"].c_str()))) {
        std::cout << "Error: RPC start failed." << std::endl;
        return 1;
    }


    for(int ind_commands = 0; ind_commands < commands.size(); ind_commands++){
        if (groups[ind_commands] == "all") {
            if (rpc.CallFunc("Task", std::string(":"), commands[ind_commands]) !=
                tirpc::rpc::RpcCallError::Success) {
                std::cout<<"CallFunc Task failed!" << std::endl;
            }
        }
        else {
            if (rpc.CallFunc("Task", groups[ind_commands], commands[ind_commands]) !=
                tirpc::rpc::RpcCallError::Success) {
                std::cout<<"CallFunc Task failed!" << std::endl;
            }
        }
    }

    while (g_loop) {
        std::vector<std::string> logs;
        g_order.SwapLogs(logs);
        if (logs.size() > 0) {
            for (const auto& log : logs) {
                std::stringstream ss;
                ss << "===== [RECEIVE RESULT] =====" << std::endl
                    <<               log              << std::endl
                    << "----------------------------" << std::endl
                    << std::endl; // Add one more split line.
                std::cout << ss.str();
                ofs << std::endl << log;

                worker_count++;
                std::cout << "worker_count = " << worker_count << std::endl;
                if(worker_count == workers) {
                    g_loop = false;
                }
            }
        } else {
            std::this_thread::sleep_for( // 100Hz
                std::chrono::milliseconds(10));
        }

        auto end = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
        if(duration.count() >= timeout)
        {
            ofs << std::endl << "Timeout! Over "<< timeout << "seconds!" << std::endl;
            return -3;
        }
    }

    if (!rpc.Stop()) {
        std::cout << "Error: Async broadcast rpc stop failed." << std::endl;
    }

    auto end = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
    // double spendTime = double(duration.count()) * std::chrono::microseconds::period::num / std::chrono::microseconds::period::den;
    // std::cout<<"Spend " << spendTime << "seconds." << std::endl;
    // ofs << std::endl << "Spend time: " << spendTime << "seconds." << std::endl;
    std::cout<<"Spend " << duration.count() << "seconds." << std::endl;
    ofs << std::endl << "Spend time: " << duration.count() << "seconds." << std::endl;

    ofs.close();
    return 0;
}
