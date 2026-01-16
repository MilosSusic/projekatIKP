#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <chrono>

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define PORT 8080

#pragma pack(push, 1)
struct MessageHeader {
    int client_id;
    int request_type;   // 4 = MESSAGE
    int payload_len;
};
#pragma pack(pop)

std::atomic<int> sent_messages{ 0 };
std::atomic<int> failed_messages{ 0 };

// Funkcija koja šalje M poruka sa jednog klijenta
void flood_client(int client_id, int message_count) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        failed_messages += message_count;
        return;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server.sin_addr);

    if (connect(sock, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        failed_messages += message_count;
        closesocket(sock);
        return;
    }

    // Opcionalno: REGISTER klijenta
    std::string username = "flood_" + std::to_string(client_id);
    MessageHeader reg_hdr{ 0, 1, (int)username.size() };
    send(sock, (char*)&reg_hdr, sizeof(reg_hdr), 0);
    send(sock, username.c_str(), reg_hdr.payload_len, 0);

    // Flood poruke
    for (int i = 0; i < message_count; i++) {
        std::string msg = "Message " + std::to_string(i) + " from client " + std::to_string(client_id);
        MessageHeader hdr{ 0, 4, (int)msg.size() };

        int n = send(sock, (char*)&hdr, sizeof(hdr), 0);
        n += send(sock, msg.c_str(), hdr.payload_len, 0);

        if (n > 0) sent_messages++;
        else failed_messages++;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    closesocket(sock);
}

int main(int argc, char* argv[]) {
    int CLIENTS = 100;       // broj klijenata
    int MESSAGES = 1000;     // poruke po klijentu

    if (argc > 1) CLIENTS = atoi(argv[1]);
    if (argc > 2) MESSAGES = atoi(argv[2]);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(CLIENTS);

    for (int i = 0; i < CLIENTS; i++) {
        threads.emplace_back(flood_client, i, MESSAGES);
    }

    for (auto& t : threads) t.join();

    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "\n===== MESSAGE FLOOD RESULT =====\n";
    std::cout << "Clients: " << CLIENTS << "\n";
    std::cout << "Messages per client: " << MESSAGES << "\n";
    std::cout << "Sent: " << sent_messages << "\n";
    std::cout << "Failed: " << failed_messages << "\n";
    std::cout << "Time(ms): "
        << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
        << "\n";

    std::cout << "Press ENTER to exit...";
    std::cin.get();

    WSACleanup();
    return 0;
}