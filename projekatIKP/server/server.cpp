
#include <iostream>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 8080
#define BUF_SIZE 4096

#pragma pack(push, 1)
struct MessageHeader {
    int client_id;
    int request_type;   // REGISTER=1, LIST=2, CONNECT_REQUEST=3, MESSAGE=4, DISCONNECT=5
    // INCOMING_CALL=6, CALL_ACCEPTED=7, CALL_REJECTED=8
    int payload_len;
};
#pragma pack(pop)

struct CircularBuffer {
    char data[BUF_SIZE];
    int head = 0;
    int tail = 0;

    bool isEmpty() const { return head == tail; }
    bool isFull() const { return ((head + 1) % BUF_SIZE) == tail; }

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
    char username[32];
    int connected_to = -1;
    int pending_requester = -1;
    CircularBuffer buffer;
    bool active = true;
};

struct Node {
    Client data;
    Node* next;
};

struct ClientList {
    Node* head = nullptr;

    Node* add(const Client& c) {
        Node* newNode = new Node{ c, nullptr };
        if (!head) {
            head = newNode;
        }
        else {
            Node* temp = head;
            while (temp->next) temp = temp->next;
            temp->next = newNode;
        }
        return newNode;
    }

    void remove(SOCKET sock);

    Client* findById(int id) {
        Node* temp = head;
        while (temp) {
            if (temp->data.id == id) return &temp->data;
            temp = temp->next;
        }
        return nullptr;
    }

    Client* findByUsername(const char* name) {
        Node* temp = head;
        while (temp) {
            if (strcmp(temp->data.username, name) == 0) return &temp->data;
            temp = temp->next;
        }
        return nullptr;
    }
};

ClientList clients;
int next_id = 1;

// Forward deklaracija
void send_message(SOCKET sock, int client_id, int type, const std::string& payload);

void ClientList::remove(SOCKET sock) {
    Node* temp = head;
    Node* prev = nullptr;
    while (temp) {
        if (temp->data.sock == sock) {
            // Ispis na server konzoli 
            std::cout << "Klijent " << temp->data.username << " (id=" << temp->data.id << ") se diskonektovao.\n";
            // Obavesti povezanog klijenta, ako postoji veza
            if (temp->data.connected_to != -1) {
                Client* other = findById(temp->data.connected_to);
                if (other) {
                    other->connected_to = -1;
                    send_message(other->sock, other->id, 5,
                        std::string("Prekinuta je komunikacija sa klijentom ") + temp->data.username);
                }
            }
            // Ukloni ?vor iz liste
            if (prev) prev->next = temp->next;
            else head = temp->next;
            delete temp;
            return;
        }
        prev = temp;
        temp = temp->next;
    }
}

void send_message(SOCKET sock, int client_id, int type, const std::string& payload) {
    MessageHeader hdr{ client_id, type, (int)payload.size() };
    send(sock, (char*)&hdr, sizeof(hdr), 0);
    if (hdr.payload_len > 0) {
        send(sock, payload.c_str(), hdr.payload_len, 0);
    }
}

void handle_request(Client& client, const MessageHeader& hdr, const std::string& payload) {
    switch (hdr.request_type) {
    case 1: { // REGISTER
        client.id = next_id++;
        strncpy_s(client.username, sizeof(client.username), payload.c_str(), _TRUNCATE);
        std::cout << "Registrovan klijent: " << client.username << " id=" << client.id << "\n";
        send_message(client.sock, client.id, 1, "REGISTER_OK");
        break;
    }
    case 2: { // LIST
        std::string list;
        Node* temp = clients.head;
        while (temp) {
            list += "client_id=" + std::to_string(temp->data.id) + " username=" + temp->data.username + "\n";
            temp = temp->next;
        }
        send_message(client.sock, client.id, 2, list);
        break;
    }
    case 3: { // CONNECT_REQUEST
        Client* target = clients.findByUsername(payload.c_str());
        if (target) {
            send_message(target->sock, target->id, 6, std::string("INCOMING_CALL from ") + client.username);
            target->pending_requester = client.id;
        }
        else {
            send_message(client.sock, client.id, 3, "CONNECT_FAILED");
        }
        break;
    }
    case 7: { // CALL_ACCEPTED
        Client* requester = clients.findById(client.pending_requester);
        if (requester) {
            requester->connected_to = client.id;
            client.connected_to = requester->id;
            send_message(requester->sock, requester->id, 3, std::string("CONNECTED to ") + client.username);
            send_message(client.sock, client.id, 3, std::string("CONNECTED to ") + requester->username);
        }
        break;
    }
    case 8: { // CALL_REJECTED
        Client* requester = clients.findById(client.pending_requester);
        if (requester) {
            send_message(requester->sock, requester->id, 3, "CONNECT_REJECTED");
        }
        break;
    }
    case 4: { // MESSAGE
        if (client.connected_to != -1) {
            Client* target = clients.findById(client.connected_to);
            if (target) {
                send_message(target->sock, client.id, 4, payload);
            }
        }
        break;
    }
    case 5: { // DISCONNECT
        if (client.connected_to != -1) {
            Client* other = clients.findById(client.connected_to);
            if (other) {
                other->connected_to = -1;
                send_message(other->sock, other->id, 5,
                    std::string("Prekinuta je komunikacija sa klijentom ") + client.username);
            }
        }
        client.connected_to = -1;
        send_message(client.sock, client.id, 5, "DISCONNECTED");
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
            clients.remove(client.sock);
            break;
        }

        client.buffer.push(recvbuf, n);

        while (client.buffer.available() >= sizeof(MessageHeader)) {
            MessageHeader hdr;
            char hdrbuf[sizeof(MessageHeader)];
            if (!client.buffer.peek(hdrbuf, sizeof(hdr))) break;
            memcpy(&hdr, hdrbuf, sizeof(hdr));

            if (client.buffer.available() < (int)(sizeof(hdr) + hdr.payload_len)) break;

            client.buffer.pop(hdrbuf, sizeof(hdr));
            memcpy(&hdr, hdrbuf, sizeof(hdr));

            std::string payload(hdr.payload_len, '\0');
            if (hdr.payload_len > 0) {
                client.buffer.pop(&payload[0], hdr.payload_len);
            }

            handle_request(client, hdr, payload);
        }
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

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
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

    std::cout << "Server pokrenut na portu " << PORT << "\n";

    while (true) {
        SOCKET client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd == INVALID_SOCKET) {
            std::cerr << "accept failed\n";
            continue;
        }

        Client c{};
        c.sock = client_fd;
        c.id = -1;
        c.username[0] = '\0';
        c.connected_to = -1;
        c.pending_requester = -1;
        c.active = true;

        Node* node = clients.add(c);
        std::thread(client_handler, std::ref(node->data)).detach();
    }

    // Cleanup (ne?e se dosti?i u ovoj petlji, ali dobro je imati)
    closesocket(server_fd);
    WSACleanup();
    return 0;
}
