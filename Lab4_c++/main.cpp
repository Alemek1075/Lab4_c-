#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <thread>
#include <shared_mutex>
#include <mutex>
#include <random>
#include <chrono>
#include <iomanip>
#include <functional>

class ThreadSafeData {
    int data[2];
    mutable std::shared_mutex mutexes[2];

public:
    ThreadSafeData() {
        data[0] = 0;
        data[1] = 0;
    }

    int get(int index) {
        if (index < 0 || index > 1) return -1;
        std::shared_lock<std::shared_mutex> lock(mutexes[index]);
        return data[index];
    }

    void set(int index, int value) {
        if (index < 0 || index > 1) return;
        std::unique_lock<std::shared_mutex> lock(mutexes[index]);
        data[index] = value;
    }

    operator std::string() {
        std::shared_lock<std::shared_mutex> lock0(mutexes[0]);
        std::shared_lock<std::shared_mutex> lock1(mutexes[1]);
        return std::to_string(data[0]) + " " + std::to_string(data[1]);
    }
};

enum OperationType {
    READ,
    WRITE,
    STRING_OP
};

struct Command {
    OperationType type;
    int index;
    int value;
};

void generate_file(const std::string& filename, int count, const std::vector<double>& probs) {
    std::ofstream file(filename);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 100.0);

    for (int i = 0; i < count; ++i) {
        double p = dis(gen);
        if (p < probs[0]) {
            file << "read 0\n";
        }
        else if (p < probs[0] + probs[1]) {
            file << "write 0 1\n";
        }
        else if (p < probs[0] + probs[1] + probs[2]) {
            file << "read 1\n";
        }
        else if (p < probs[0] + probs[1] + probs[2] + probs[3]) {
            file << "write 1 1\n";
        }
        else {
            file << "string\n";
        }
    }
    file.close();
}

std::vector<Command> load_commands(const std::string& filename) {
    std::vector<Command> commands;
    std::ifstream file(filename);
    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string op;
        ss >> op;
        Command cmd;
        if (op == "read") {
            cmd.type = READ;
            ss >> cmd.index;
        }
        else if (op == "write") {
            cmd.type = WRITE;
            ss >> cmd.index >> cmd.value;
        }
        else {
            cmd.type = STRING_OP;
        }
        commands.push_back(cmd);
    }
    return commands;
}

void worker(ThreadSafeData& data, const std::vector<Command>& commands, int start, int end) {
    for (int i = start; i < end; ++i) {
        const auto& cmd = commands[i];
        if (cmd.type == READ) {
            volatile int val = data.get(cmd.index);
        }
        else if (cmd.type == WRITE) {
            data.set(cmd.index, cmd.value);
        }
        else {
            volatile std::string s = std::string(data);
        }
    }
}

long long measure(int num_threads, const std::string& filename) {
    ThreadSafeData data;
    auto commands = load_commands(filename);
    int total_cmds = commands.size();
    int cmds_per_thread = total_cmds / num_threads;

    std::vector<std::thread> threads;
    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_threads; ++i) {
        int start = i * cmds_per_thread;
        int end = (i == num_threads - 1) ? total_cmds : start + cmds_per_thread;
        threads.emplace_back(worker, std::ref(data), std::ref(commands), start, end);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
}

int main() {
    const int CMD_COUNT = 100000;

    std::vector<double> probs_variant = { 20.0, 5.0, 20.0, 5.0, 50.0 };
    generate_file("variant_16.txt", CMD_COUNT, probs_variant);

    std::vector<double> probs_equal = { 20.0, 20.0, 20.0, 20.0, 20.0 };
    generate_file("equal.txt", CMD_COUNT, probs_equal);

    std::vector<double> probs_skewed = { 0.0, 50.0, 0.0, 50.0, 0.0 };
    generate_file("skewed.txt", CMD_COUNT, probs_skewed);

    std::vector<std::string> files = { "variant_16.txt", "equal.txt", "skewed.txt" };
    std::vector<int> thread_counts = { 1, 2, 3 };

    std::cout << std::left << std::setw(20) << "File"
        << std::setw(10) << "Threads"
        << std::setw(15) << "Time (ms)" << std::endl;
    std::cout << std::string(45, '-') << std::endl;

    for (const auto& file : files) {
        for (int t : thread_counts) {
            long long time = measure(t, file);
            std::cout << std::left << std::setw(20) << file
                << std::setw(10) << t
                << std::setw(15) << time << std::endl;
        }
        std::cout << std::string(45, '-') << std::endl;
    }

    return 0;
}