#include <iostream>
#include <cstring>
#include <thread>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <unordered_map>
#include <string>
#include <mutex>
#include <sstream>
#include <chrono>
#include <fstream>

std::unordered_map<std::string, std::string> database;
std::unordered_map<std::string, std::chrono::steady_clock::time_point> expiry;
std::mutex db_mutex;

void loadDatabase() 
{
    std::ifstream file("database.txt");
    std::lock_guard<std::mutex> lock(db_mutex);
    std::string line;
    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string key;
        std::string value;
        std::string ttl;
        std::getline(ss, key, '|');
        std::getline(ss, value, '|');
        std::getline(ss, ttl, '|');
        database[key]= value;
        long long remaining_seconds= std::stoll(ttl);
        if (remaining_seconds >0) {
            expiry[key]= std::chrono::steady_clock::now() + std::chrono::seconds(remaining_seconds);
        }
    }
    file.close();
}

void saveDatabase() 
{
    std::ofstream file("database.txt");
    std::lock_guard<std::mutex> lock(db_mutex);
    for (const auto& entry: database)
    {
        long long remaining_seconds=0;
        auto expiry_it= expiry.find(entry.first);
        if (expiry_it != expiry.end()) {
            auto remaining= std::chrono::duration_cast<std::chrono::seconds>(expiry_it->second - std::chrono::steady_clock::now());
            remaining_seconds=remaining.count();
        }
        file << entry.first << "|" 
        << entry.second << "|"
        << remaining_seconds << "\n";
    }
    file.close();
}

void handleClient(int client_fd)
{
    std::string input_buffer;
    while (true)
    {
        char buffer[1024] = {0};
        int bytes_received = recv(
            client_fd,
            buffer,
            sizeof(buffer),
            0);
        if (bytes_received <= 0)
        {
            break;
        }
        input_buffer.append(buffer, bytes_received);
        size_t newline_pos;
        while ((newline_pos = input_buffer.find('\n')) != std::string::npos)
        {
            std::string command_line = input_buffer.substr(0, newline_pos);
            input_buffer.erase(0, newline_pos + 1);
            std::stringstream ss(command_line);
            std::string command;
            ss >> command;
            if (command == "PING")
            {
                const char *response = "PONG\n";
                send(client_fd, response, std::strlen(response), 0);
            }
            else if (command == "SET")
            {
                std::string key;
                ss >> key;
                if (key.empty()){
                    const char* response= "ERR missing key\n";
                    send(client_fd, response, std::strlen(response),0);
                    continue;
                }
                std::string value;
                std::getline(ss, value);
                if (!value.empty() && value[0] == ' ')
                {
                    value.erase(0, 1);
                }
                if (value.empty()) {
                    const char* response= "ERR missing value\n";
                    send(client_fd, response, std::strlen(response), 0);
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    database[key] = value;
                    expiry.erase(key);
                }
                const char *response = "OK\n";
                send(client_fd, response, std::strlen(response), 0);
            }
            else if (command=="EXPIRE"){
                std::string key;
                ss >> key;
                if (key.empty()) {
                    const char* response= "ERR missing key\n";
                    send (client_fd, response, std::strlen(response), 0);
                    continue;
                }
                int seconds;
                ss >> seconds;
                if (ss.fail()) {
                    const char* response= "ERR missing seconds\n";
                    send (client_fd, response, std::strlen(response),0);
                    continue;
                }
                std::lock_guard<std::mutex> lock(db_mutex);
                if (database.find(key)==database.end()) {
                    const char* response= "0\n";
                    send(client_fd, response, std::strlen(response),0);
                    continue;
                }
                expiry[key] = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
                const char* response= "1\n";
                send(client_fd, response, std::strlen(response),0);

            }
            else if (command == "GET")
            {
                std::string key;
                ss >> key;
                if (key.empty()){
                    const char* response= "ERR missing key\n";
                    send(client_fd, response, std::strlen(response),0);
                    continue;
                }
                std::string value;
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    auto it = database.find(key);
                    if (it != database.end())
                    {
                        auto expiry_it= expiry.find(key);
                        if (expiry_it != expiry.end() && std::chrono::steady_clock::now() >= expiry_it -> second){
                            database.erase(key);
                            expiry.erase(key);
                            value="(nil)";

                        }
                        else {
                            value = it->second;
                        }
                    }
                    else
                    {
                        value = "(nil)";
                    }
                }
                value += "\n";
                send(client_fd, value.c_str(), value.size(), 0);
            }
            else if (command == "EXISTS")
            {
                std::string key;
                ss >> key;
                if (key.empty()){
                    const char* response= "ERR missing key\n";
                    send(client_fd, response, std::strlen(response),0);
                    continue;
                }
                int exists = 0;
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    auto it= database.find(key);
                    if (it != database.end()) {
                        auto expiry_it= expiry.find(key);
                        if (expiry_it != expiry.end() && std::chrono::steady_clock::now() >= expiry_it->second){
                            database.erase(key);
                            expiry.erase(key);
                        }
                        else {
                            exists=1;
                        }
                    }
                }
                std::string response = std::to_string(exists) + "\n";
                send(client_fd, response.c_str(), response.size(), 0);
            }
            else if (command == "DEL")
            {
                std::string key;
                ss >> key;
                if (key.empty()){
                    const char* response= "ERR missing key\n";
                    send(client_fd, response, std::strlen(response),0);
                    continue;
                }
                int deleted = 0;
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    if (database.erase(key) > 0)
                    {
                        expiry.erase(key);
                        deleted = 1;
                    }
                }
                std::string response = std::to_string(deleted) + "\n";
                send(client_fd, response.c_str(), response.size(), 0);
            }
            else if (command=="SAVE") {
                saveDatabase();
                const char* response= "OK\n";
                send(client_fd, response, std::strlen(response),0);
            }
            else {
                const char* response= "ERR unknown command\n";
                send(client_fd, response, std::strlen(response),0);
            }
        }
    }
    std::cout << "Client disconnected.\n";
    close(client_fd);
}

int main()
{
    loadDatabase();
    // 1. Create a TCP socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    // 2. Define the server address
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6379);

    // 3. Bind the socket to port 6379
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        std::cerr << "Bind failed\n";
        close(server_fd);
        return 1;
    }

    // 4. Start listening for clients
    if (listen(server_fd, 10) < 0)
    {
        std::cerr << "Listen failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Mini Redis server running on port 6379...\n";

    // 5. Receive a command
    while (true)
    {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0)
        {
            std::cerr << "Accept failed\n";
            continue;
        }
        std::cout << "Client connected!\n";
        std::thread client_thread(handleClient, client_fd);
        client_thread.detach();
    }
    return 0;
}