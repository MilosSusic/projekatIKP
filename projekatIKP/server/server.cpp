
#include <iostream>
#include <string>
#include <list>
#include <thread>
#include <mutex>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 8080
#define BUF_SIZE 4096

#pragma pack(push, 1)
struct MessageHeader {
    int client_id;
    int request_type;   // REGISTER=1, LIST=2, CONNECT=3, MESSAGE=4, DISCONNECT=5
    int payload_len;
};
#pragma pack(pop)

struct CircularBuffer {
    char data[BUF_SIZE];
    int head = 0;
    int tail = 0;

    bool isEmpty() const { return head == tail; }
    bool isFull() const { return (head + 1) % BUF_SIZE == tail; }

    int available() const {
        if (head >= tail) return head - tail;
        return BUF_SIZE - tail + head;
    }

    int push(const char* src, int len) {
        int pushed = 0;
        for (int i = 0; i < len; i++) {
            if (isFull()) break;
            data[head] = src[i];
            head = (head + 1) % BUF_SIZE;
            pushed++;
        }
        return pushed;
    }

    bool pop(char* dst, int len) {
        if (available() < len) return false;
        for (int i = 0; i < len; i++) {
            dst[i] = data[tail];
            tail = (tail + 1) % BUF_SIZE;
        }
        return true;
    }

    bool peek(char* dst, int len) const {
        if (available() < len) return false;
        int t = tail;
        for (int i = 0; i < len; i++) {
            dst[i] = data[t];
            t = (t + 1) % BUF_SIZE;
        }
        return true;
    }
};

struct Client {
    int id = -1;
    SOCKET sock;
    std::string username;
    int connected_to = -1;
    CircularBuffer buffer;
};

std::list<Client> clients;
std::mutex clients_mutex;
int next_id = 1;

void send_message(SOCKET sock, int client_id, int type, const std::string& payload) {
    MessageHeader hdr{ client_id, type, (int)payload.size() };
    send(sock, (char*)&hdr, sizeof(hdr), 0);
    if (hdr.payload_len > 0) send(sock, payload.c_str(), hdr.payload_len, 0);
}

Client* find_client_by_id(int id) {
    for (auto& c : clients) if (c.id == id) return &c;
    return nullptr;
}
Client* find_client_by_username(const std::string& name) {
    for (auto& c : clients) if (c.username == name) return &c;
    return nullptr;
}
Client* find_client_by_socket(SOCKET sock) {
    for (auto& c : clients) if (c.sock == sock) return &c;
    return nullptr;
}

void remove_client(SOCKET sock) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto it = clients.begin(); it != clients.end(); ++it) {
        if (it->sock == sock) {
            std::cout << "Klijent " << it->username << " se diskonektovao.\n";
            clients.erase(it);
            break;
        }
    }
}

void handle_request(Client& client, const MessageHeader& hdr, const std::string& payload) {
    switch (hdr.request_type) {
    case 1: { // REGISTER
        client.id = next_id++;
        client.username = payload;
        std::cout << "Registrovan klijent: " << client.username << " id=" << client.id << "\n";
        send_message(client.sock, client.id, 1, "REGISTER_OK");
        break;
    }
    case 2: { // LIST
        std::string list;
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& c : clients) {
            list += "client_id=" + std::to_string(c.id) + " username=" + c.username + "\n";
        }
        send_message(client.sock, client.id, 2, list);
        break;
    }
    case 3: { // CONNECT
        Client* target = find_client_by_username(payload);
        if (target) {
            client.connected_to = target->id;
            target->connected_to = client.id;
            send_message(client.sock, client.id, 3, "CONNECTED to " + target->username);
            send_message(target->sock, target->id, 3, "CONNECTED to " + client.username);
        }
        else {
            send_message(client.sock, client.id, 3, "CONNECT_FAILED");
        }
        break;
    }
    case 4: { // MESSAGE
        if (client.connected_to != -1) {
            Client* target = find_client_by_id(client.connected_to);
            if (target) {
                send_message(target->sock, client.id, 4, payload);
            }
        }
        break;
    }
    case 5: { // DISCONNECT
        shutdown(client.sock, SD_BOTH);
        closesocket(client.sock);
        remove_client(client.sock);
        break;
    }
    default:
        send_message(client.sock, client.id, 0, "UNKNOWN_REQUEST");
    }
}

void client_handler(Client& client) {
    char recvbuf[512];
    while (true) {
        int n = recv(client.sock, recvbuf, sizeof(recvbuf), 0);
        if (n <= 0) {
            remove_client(client.sock);
            break;
        }
        client.buffer.push(recvbuf, n);

        // Parsiranje poruka iz bafera
        while (client.buffer.available() >= sizeof(MessageHeader)) {
            MessageHeader hdr;
            char hdrbuf[sizeof(MessageHeader)];
            if (!client.buffer.peek(hdrbuf, sizeof(hdr))) break;
            memcpy(&hdr, hdrbuf, sizeof(hdr));

            if (client.buffer.available() < sizeof(hdr) + hdr.payload_len) break;

            // sada sigurno imamo celu poruku
            client.buffer.pop(hdrbuf, sizeof(hdr));
            memcpy(&hdr, hdrbuf, sizeof(hdr));

            std::string payload(hdr.payload_len, '\0');
            client.buffer.pop(&payload[0], hdr.payload_len);

            handle_request(client, hdr, payload);
        }
    }
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, SOMAXCONN);

    std::cout << "Server pokrenut na portu " << PORT << "\n";

    while (true) {
        SOCKET client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd == INVALID_SOCKET) continue;

        Client c{};
        c.sock = client_fd;
        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.push_back(c);
            Client& ref = clients.back();
            std::thread(client_handler, std::ref(ref)).detach();
        }
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}



