

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 8080

#pragma pack(push, 1)
struct MessageHeader {
    int client_id;
    int request_type;   // REGISTER=1, LIST=2, CONNECT=3, MESSAGE=4, DISCONNECT=5
    int payload_len;
};
#pragma pack(pop)

struct Client {
    int id;
    SOCKET socket_fd;
    std::string username;
    int connected_to; // id klijenta sa kojim je povezan (-1 ako nije povezan)
};

std::vector<Client> clients;
std::mutex clients_mutex;
int next_id = 1;

void print_connected_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    std::cout << "\n=== Lista povezanih klijenata ===\n";
    for (auto& c : clients) {
        std::cout << "client_id=" << c.id
            << " username=" << c.username
            << " connected_to=" << c.connected_to << std::endl;
    }
    if (clients.empty()) {
        std::cout << "(nema povezanih klijenata)" << std::endl;
    }
    std::cout << "===============================\n";
}

void send_message(SOCKET sock, int client_id, int type, const std::string& payload) {
    MessageHeader hdr{ client_id, type, (int)payload.size() };
    send(sock, (char*)&hdr, sizeof(hdr), 0);
    if (hdr.payload_len > 0) send(sock, payload.c_str(), hdr.payload_len, 0);
}

Client* find_client_by_username(const std::string& username) {
    for (auto& c : clients) {
        if (c.username == username) return &c;
    }
    return nullptr;
}

Client* find_client_by_socket(SOCKET sock) {
    for (auto& c : clients) {
        if (c.socket_fd == sock) return &c;
    }
    return nullptr;
}

Client* find_client_by_id(int id) {
    for (auto& c : clients) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

void remove_client(SOCKET sock) {
    int removed_id = -1;
    std::string removed_username;

    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto it = clients.begin(); it != clients.end(); ++it) {
            if (it->socket_fd == sock) {
                removed_id = it->id;
                removed_username = it->username;
                clients.erase(it);
                break;
            }
        }
    }

    if (removed_id != -1) {
        std::cout << "Klijent client_id=" << removed_id
            << " username=" << removed_username
            << " se diskonektovao." << std::endl;
        print_connected_clients();
    }
}

void handle_request(SOCKET sock, const MessageHeader& hdr, const std::string& payload) {
    switch (hdr.request_type) {
    case 1: { // REGISTER
        int assigned_id = next_id++;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.push_back({ assigned_id, sock, payload, -1 });
        }
        std::cout << "Klijent registrovan: id=" << assigned_id
            << " username=" << payload << std::endl;
        send_message(sock, assigned_id, 1, "REGISTER_OK");
        break;
    }
    case 2: { // LIST
        std::string list;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto& c : clients) {
                list += "payload=" + c.username + "\n"
                    + "client_id=" + std::to_string(c.id)
                    + " type=REGISTER\n";
            }
        }
        send_message(sock, hdr.client_id, 2, list);
        break;
    }
    case 3: { // CONNECT
        Client* requester = find_client_by_socket(sock);
        Client* target = find_client_by_username(payload);
        if (requester && target) {
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                requester->connected_to = target->id;
                target->connected_to = requester->id;
            }
            send_message(sock, requester->id, 3, "CONNECTED to " + target->username);
            send_message(target->socket_fd, target->id, 3, "CONNECTED to " + requester->username);
        }
        else {
            send_message(sock, hdr.client_id, 3, "CONNECT_FAILED: user not found");
        }
        break;
    }
    case 4: { // MESSAGE
        Client* sender = find_client_by_socket(sock);
        if (sender && sender->connected_to != -1) {
            Client* target = find_client_by_id(sender->connected_to);
            if (target) {
                send_message(target->socket_fd, sender->id, 4, payload);
            }
            else {
                send_message(sock, sender->id, 4, "Target not available");
            }
        }
        else {
            send_message(sock, hdr.client_id, 4, "No active connection");
        }
        break;
    }
    case 5: { // DISCONNECT
        shutdown(sock, SD_BOTH);
        closesocket(sock);
        remove_client(sock);
        break;
    }
    default:
        send_message(sock, hdr.client_id, 0, "UNKNOWN_REQUEST");
    }
}

void client_handler(SOCKET client_fd) {
    while (true) {
        MessageHeader hdr;
        int n = recv(client_fd, (char*)&hdr, sizeof(hdr), 0);
        if (n <= 0) {
            shutdown(client_fd, SD_BOTH);
            closesocket(client_fd);
            remove_client(client_fd);
            break;
        }

        std::string payload;
        if (hdr.payload_len > 0) {
            payload.resize(hdr.payload_len);
            int m = recv(client_fd, &payload[0], hdr.payload_len, 0);
            if (m <= 0) {
                shutdown(client_fd, SD_BOTH);
                closesocket(client_fd);
                remove_client(client_fd);
                break;
            }
        }

        handle_request(client_fd, hdr, payload);
    }
}

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "socket failed\n";
        WSACleanup();
        return 1;
    }

    BOOL opt = TRUE;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "bind failed\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "listen failed\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    std::cout << "Server pokrenut na portu " << PORT << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        int addrlen = sizeof(client_addr);
        SOCKET client_fd = accept(server_fd, (sockaddr*)&client_addr, &addrlen);
        if (client_fd == INVALID_SOCKET) continue;
        std::thread(client_handler, client_fd).detach();
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}

