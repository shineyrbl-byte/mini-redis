#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <chrono>
#include <thread>

void runClient(int requests)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        std::cerr << "Socket creation failed\n";
        return;
    }
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(6379);
    inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr);
    if (connect(sock, (sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        std::cerr << "Connection failed\n";
        close(sock);
        return;
    }
    for (int i = 0; i < requests; i++)
    {
        const char *request = "PING\n";
        send(sock, request, std::strlen(request), 0);
        char buffer[1024] = {0};
        int bytes_received =
            recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0)
        {
            std::cerr << "Request failed\n";
            break;
        }
    }
    close(sock);
}

int main()
{
    const int NUM_CLIENTS = 10;
    const int REQUESTS_PER_CLIENT = 1000;
    std::thread clients[NUM_CLIENTS];
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < NUM_CLIENTS; i++)
    {
        clients[i] = std::thread(runClient, REQUESTS_PER_CLIENT);
    }
    for (int i = 0; i < NUM_CLIENTS; i++)
    {
        clients[i].join();
    }
    auto end = std::chrono::steady_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start);
    int total_requests =
        NUM_CLIENTS * REQUESTS_PER_CLIENT;
    std::cout << "Time taken: "
              << elapsed.count()
              << " microseconds\n";
    std::cout << "Total requests: "
              << total_requests
              << "\n";
    double seconds = elapsed.count() / 1000000.0;
    double requests_per_second =
        total_requests / seconds;
    std::cout << "Requests per second: "
              << requests_per_second
              << "\n";
    return 0;
}