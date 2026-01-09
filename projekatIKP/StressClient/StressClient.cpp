#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define PORT 8080

#pragma pack(push, 1)
struct MessageHeader {
    int client_id;
    int request_type;
    int payload_len;
};
#pragma pack(pop)

std::atomic<int> success_connections{ 0 };
std::atomic<int> failed_connections{ 0 };

void stress_client(int id) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        failed_connections++;
        return;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

    if (connect(sock, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        failed_connections++;
        closesocket(sock);
        return;
    }

    // REGISTER
    std::string username = "user_" + std::to_string(id);
    MessageHeader hdr{ 0, 1, (int)username.size() };

    send(sock, (char*)&hdr, sizeof(hdr), 0);
    send(sock, username.c_str(), hdr.payload_len, 0);

    success_connections++;

    // Ostavimo konekciju otvorenu (ključni deo)
    std::this_thread::sleep_for(std::chrono::seconds(60));

    closesocket(sock);
}

int main(int argc, char* argv[]) {
    int CLIENT_COUNT = 1000;

    if (argc > 1) {
        CLIENT_COUNT = atoi(argv[1]);
    }

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(CLIENT_COUNT);

    for (int i = 0; i < CLIENT_COUNT; i++) {
        threads.emplace_back(stress_client, i);
        std::this_thread::sleep_for(std::chrono::milliseconds(2)); // sprečava burst
    }

    for (auto& t : threads)

        t.join();

    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "\n===== STRESS TEST RESULT =====\n";
    std::cout << "Requested clients: " << CLIENT_COUNT << "\n";
    std::cout << "Connected: " << success_connections << "\n";
    std::cout << "Failed: " << failed_connections << "\n";
    std::cout << "Time: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
        << " ms\n";

    WSACleanup();
    std::cout << "\nPritisni ENTER za izlaz...";
    std::cin.get();
    return 0;
}

