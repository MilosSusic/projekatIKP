
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Protocol.h"
#include "NetConstants.h"
#include "Message.h"

#pragma comment(lib, "Ws2_32.lib")

#define SERVER_IP "127.0.0.1"

SOCKET sock;
int my_id = 0;

void print_menu() {
    std::cout << "\n=== Meni ===\n";
    std::cout << "1. Register\n";
    std::cout << "2. List clients\n";
    std::cout << "3. Connect to client\n";
    std::cout << "4. Send message\n";
    std::cout << "5. Disconnect\n";
    std::cout << "6. Accept call\n";
    std::cout << "7. Reject call\n";
    std::cout << "Izbor: ";
}

void send_request(int type, const std::string& payload) {
    MessageHeader hdr{ my_id, type, (int)payload.size() };
    send(sock, (char*)&hdr, sizeof(hdr), 0);
    if (hdr.payload_len > 0) send(sock, payload.c_str(), hdr.payload_len, 0);
}

void recv_thread() {
    while (true) {
        MessageHeader hdr;
        int n = recv(sock, (char*)&hdr, sizeof(hdr), 0);
        if (n <= 0) {
            std::cout << "Veza sa serverom prekinuta." << std::endl;
            break;
        }

        std::string payload;
        if (hdr.payload_len > 0) {
            payload.resize(hdr.payload_len);
            recv(sock, &payload[0], hdr.payload_len, 0);
        }

        if (hdr.request_type == 6) { // INCOMING_CALL
            std::cout << "\nPoziv od klijenta: " << payload
                << "\nZa prihvatanje izaberi opciju 6, za odbijanje opciju 7 u meniju."
                << std::endl;
        }
        else {
            std::cout << "\n[SERVER odgovor] client_id=" << hdr.client_id
                << " type=" << hdr.request_type
                << " payload=" << (payload.empty() ? "(nema)" : payload)
                << std::endl;
        }

        print_menu();
    }
}

void menu() {
    int choice;
    std::string input;

    print_menu();

    while (true) {
        std::cin >> choice;

        if (std::cin.fail()) {
            std::cin.clear(); // reset fail state
            std::cin.ignore();
            std::cout << "Nepoznata opcija." << std::endl;
            std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');
            print_menu();
            continue;
        }
        std::cin.ignore((std::numeric_limits<std::streamsize>::max)(), '\n');

        switch (choice) {
        case 1: // REGISTER
            std::cout << "Unesi username: ";
            std::getline(std::cin, input);
            send_request(1, input);
            break;
        case 2: // LIST
            send_request(2, "");
            break;
        case 3: // CONNECT
            std::cout << "Unesi ime klijenta: ";
            std::getline(std::cin, input);
            send_request(3, input);
            break;
        case 4: // MESSAGE
            std::cout << "Unesi poruku: ";
            std::getline(std::cin, input);
            send_request(4, input);
            break;
        case 5: // DISCONNECT
            send_request(5, "");
            return;
        case 6: // ACCEPT CALL
            send_request(7, ""); // CALL_ACCEPTED
            break;
        case 7: // REJECT CALL
            send_request(8, ""); // CALL_REJECTED
            break;
        default:
            std::cout << "Nepoznata opcija." << std::endl;
        }
    }
}

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connect failed" << std::endl;
        return 1;
    }

    std::cout << "Klijent povezan na server " << SERVER_IP << ":" << PORT << std::endl;

    std::thread t_recv(recv_thread);
    menu();

    t_recv.detach();
    closesocket(sock);
    WSACleanup();
    return 0;
}