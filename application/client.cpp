#include <iostream>
#include <thread>
#include <cstring>
#include <sstream>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;

#define SERVER_IP "127.0.0.1"
#define TCP_PORT 8080
#define UDP_PORT 9090
#define BUFFER_SIZE 1024
#define HEARTBEAT_INTERVAL 10

// ======================
// Global Variables
// ======================

string campusName;
int tcpSocket;
bool isRunning = true;

// ======================
// Valid Data
// ======================

vector<string> validCampuses = {
    "Lahore",
    "Karachi",
    "Islamabad",
    "Peshawar",
    "CFD",
    "Multan"
};

vector<string> validDepartments = {
    "Admissions",
    "Academics",
    "IT",
    "Sports"
};

// ======================
// Utility Functions
// ======================

bool isValidCampus(const string& campus) {

    return find(validCampuses.begin(),
                validCampuses.end(),
                campus) != validCampuses.end();
}

bool isValidDepartment(const string& department) {

    return find(validDepartments.begin(),
                validDepartments.end(),
                department) != validDepartments.end();
}

bool isEmpty(const string& str) {

    return str.empty();
}

void clearInputBuffer() {

    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

// ======================
// UDP Heartbeat Thread
// ======================

void sendHeartbeat() {

    int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);

    if (udpSocket < 0) {

        cerr << "[ERROR] Failed to create UDP socket.\n";
        return;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(UDP_PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &serverAddress.sin_addr) <= 0) {

        cerr << "[ERROR] Invalid server IP address.\n";
        close(udpSocket);
        return;
    }

    sockaddr_in localAddress{};
    localAddress.sin_family = AF_INET;
    localAddress.sin_addr.s_addr = INADDR_ANY;
    localAddress.sin_port = 0;

    if (bind(udpSocket,
             (sockaddr*)&localAddress,
             sizeof(localAddress)) < 0) {

        cerr << "[ERROR] Failed to bind UDP socket.\n";
        close(udpSocket);
        return;
    }

    while (isRunning) {

        string heartbeatMessage =
            "HEARTBEAT|" + campusName;

        int sentBytes =
            sendto(udpSocket,
                   heartbeatMessage.c_str(),
                   heartbeatMessage.size(),
                   0,
                   (sockaddr*)&serverAddress,
                   sizeof(serverAddress));

        if (sentBytes < 0) {

            cerr << "[ERROR] Failed to send heartbeat.\n";
        }

        char recvBuffer[BUFFER_SIZE];
        sockaddr_in fromAddr{};
        socklen_t fromLen = sizeof(fromAddr);

        int recvBytes =
            recvfrom(udpSocket,
                     recvBuffer,
                     BUFFER_SIZE - 1,
                     MSG_DONTWAIT,
                     (sockaddr*)&fromAddr,
                     &fromLen);

        if (recvBytes > 0) {

            string message(recvBuffer, recvBytes);
            stringstream ss(message);
            string packetType;
            getline(ss, packetType, '|');

            if (packetType == "BROADCAST") {

                string announcement;
                getline(ss, announcement);

                cout << "\n========== SYSTEM BROADCAST ==========" << endl;
                cout << announcement << endl;
                cout << "======================================\n";
            }
        }

        sleep(HEARTBEAT_INTERVAL);
    }

    close(udpSocket);
}

// ======================
// Listen to Server Thread
// ======================

void listenToServer() {

    char buffer[BUFFER_SIZE];

    while (isRunning) {

        memset(buffer, 0, BUFFER_SIZE);

        int receivedBytes =
            recv(tcpSocket,
                 buffer,
                 BUFFER_SIZE - 1,
                 0);

        if (receivedBytes > 0) {

            string message(buffer);

            stringstream ss(message);

            string type;
            getline(ss, type, '|');

            // ======================
            // Incoming Message
            // ======================

            if (type == "FROM") {

                string senderCampus;
                string department;
                string text;

                getline(ss, senderCampus, '|');
                getline(ss, department, '|');
                getline(ss, text);

                cout << "\n=================================\n";
                cout << "New Message Received\n";
                cout << "From Campus : " << senderCampus << endl;
                cout << "Department  : " << department << endl;
                cout << "Message     : " << text << endl;
                cout << "=================================\n";
            }

            // ======================
            // Error Handling
            // ======================

            else if (type == "ERROR") {

                string errorType;
                getline(ss, errorType);

                if (errorType == "TARGET_OFFLINE") {

                    cout << "\n[ERROR] Target campus is offline.\n";
                }
                else {

                    cout << "\n[ERROR] Unknown server error.\n";
                }
            }

            // ======================
            // Broadcast Message
            // ======================

            else if (type == "BROADCAST") {

                string announcement;
                getline(ss, announcement);

                cout << "\n========== SYSTEM BROADCAST ==========\n";
                cout << announcement << endl;
                cout << "======================================\n";
            }

            else {

                cout << "\n[INFO] " << message << endl;
            }
        }

        // ======================
        // Server Disconnected
        // ======================

        else if (receivedBytes == 0) {

            cout << "\n[ERROR] Server disconnected.\n";

            isRunning = false;
            break;
        }

        // ======================
        // recv() Error
        // ======================

        else {

            cerr << "\n[ERROR] Failed to receive data from server.\n";

            isRunning = false;
            break;
        }
    }
}

// ======================
// Authentication
// ======================

bool authenticateClient() {

    while (true) {

        cout << "\nEnter Campus Name: ";
        getline(cin, campusName);

        if (!isValidCampus(campusName)) {

            cout << "[ERROR] Invalid campus name.\n";
            continue;
        }

        string password;

        cout << "Enter Password: ";
        getline(cin, password);

        if (isEmpty(password)) {

            cout << "[ERROR] Password cannot be empty.\n";
            continue;
        }

        // ======================
        // Create TCP Socket
        // ======================

        tcpSocket = socket(AF_INET, SOCK_STREAM, 0);

        if (tcpSocket < 0) {

            cerr << "[ERROR] Failed to create TCP socket.\n";
            return false;
        }

        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(TCP_PORT);

        if (inet_pton(AF_INET,
                      SERVER_IP,
                      &serverAddress.sin_addr) <= 0) {

            cerr << "[ERROR] Invalid server IP address.\n";

            close(tcpSocket);
            return false;
        }

        // ======================
        // Connect to Server
        // ======================

        if (connect(tcpSocket,
                    (sockaddr*)&serverAddress,
                    sizeof(serverAddress)) < 0) {

            cerr << "[ERROR] Unable to connect to server.\n";

            close(tcpSocket);
            return false;
        }

        // ======================
        // Send Login Request
        // ======================

        string loginMessage =
            "LOGIN|" + campusName + "|" + password;

        int sentBytes =
            send(tcpSocket,
                 loginMessage.c_str(),
                 loginMessage.size(),
                 0);

        if (sentBytes < 0) {

            cerr << "[ERROR] Failed to send login request.\n";

            close(tcpSocket);
            return false;
        }

        // ======================
        // Receive Server Response
        // ======================

        char buffer[BUFFER_SIZE] = {0};

        int receivedBytes =
            recv(tcpSocket,
                 buffer,
                 BUFFER_SIZE - 1,
                 0);

        if (receivedBytes <= 0) {

            cerr << "[ERROR] Server did not respond.\n";

            close(tcpSocket);
            return false;
        }

        string response(buffer);

        if (response == "AUTH|SUCCESS") {

            cout << "\n[SUCCESS] Authentication successful.\n";

            return true;
        }

        else {

            cout << "\n[ERROR] Authentication failed.\n";

            close(tcpSocket);
        }
    }
}

// ======================
// Send Message
// ======================

void sendMessage() {

    string targetCampus;
    string department;
    string message;

    cout << "\nEnter Target Campus: ";
    getline(cin, targetCampus);

    if (!isValidCampus(targetCampus)) {

        cout << "[ERROR] Invalid target campus.\n";
        return;
    }

    if (targetCampus == campusName) {

        cout << "[ERROR] Cannot send message to your own campus.\n";
        return;
    }

    cout << "Enter Department: ";
    getline(cin, department);

    if (!isValidDepartment(department)) {

        cout << "[ERROR] Invalid department.\n";
        return;
    }

    cout << "Enter Message: ";
    getline(cin, message);

    if (isEmpty(message)) {

        cout << "[ERROR] Message cannot be empty.\n";
        return;
    }

    if (message.length() > 500) {

        cout << "[ERROR] Message too long.\n";
        return;
    }

    string fullMessage =
        "MSG|" +
        targetCampus + "|" +
        department + "|" +
        message;

    int sentBytes =
        send(tcpSocket,
             fullMessage.c_str(),
             fullMessage.size(),
             0);

    if (sentBytes < 0) {

        cerr << "[ERROR] Failed to send message.\n";
    }
    else {

        cout << "[SUCCESS] Message sent to server.\n";
    }
}

// ======================
// Menu
// ======================

void showMenu() {

    while (isRunning) {

        cout << "\n========== CAMPUS MENU ==========\n";
        cout << "1. Send Message\n";
        cout << "2. Exit\n";
        cout << "Enter Choice: ";

        string input;
        getline(cin, input);

        stringstream ss(input);

        int choice;

        if (!(ss >> choice)) {

            cout << "[ERROR] Invalid input.\n";
            continue;
        }

        switch (choice) {

            case 1:
                sendMessage();
                break;

            case 2:

                cout << "\nDisconnecting from server...\n";

                isRunning = false;

                shutdown(tcpSocket, SHUT_RDWR);

                close(tcpSocket);

                return;

            default:

                cout << "[ERROR] Invalid menu choice.\n";
        }
    }
}

// ======================
// Main
// ======================

int main() {

    cout << "====================================\n";
    cout << "NU Information Exchange System\n";
    cout << "====================================\n";

    bool authenticated =
        authenticateClient();

    if (!authenticated) {

        cerr << "[ERROR] Client startup failed.\n";
        return 1;
    }

    thread heartbeatThread(sendHeartbeat);
    thread listenerThread(listenToServer);

    showMenu();

    if (heartbeatThread.joinable()) {
        heartbeatThread.join();
    }

    if (listenerThread.joinable()) {
        listenerThread.join();
    }

    cout << "\nClient closed successfully.\n";

    return 0;
}