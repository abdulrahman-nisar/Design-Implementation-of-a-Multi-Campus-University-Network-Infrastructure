#include <iostream>
#include <thread>
#include <map>
#include <vector>
#include <cstring>
#include <sstream>
#include <mutex>
#include <algorithm>
#include <ctime>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;

// =====================================
// Constants
// =====================================

#define TCP_PORT 8080
#define UDP_PORT 9090
#define BUFFER_SIZE 1024
#define MAX_PENDING_CONNECTIONS 10

// =====================================
// Structures
// =====================================

struct ClientInfo {

    int socket;

    string campus;

    bool authenticated;
};

struct CampusStatus {

    string status;

    sockaddr_in udpAddress;

    time_t lastSeen;
};

// =====================================
// Global Variables
// =====================================

map<string, ClientInfo> connectedClients;

map<string, CampusStatus> campusStatuses;

mutex clientMutex;

// =====================================
// Valid Credentials
// =====================================

vector<string> campuses = {
    "Lahore",
    "Karachi",
    "Islamabad",
    "Peshawar",
    "CFD",
    "Multan"
};

vector<string> passwords = {
    "NU-LHR-123",
    "NU-KHI-123",
    "NU-ISB-123",
    "NU-PEW-123",
    "NU-CFD-123",
    "NU-MUL-123"
};

vector<string> validDepartments = {
    "Admissions",
    "Academics",
    "IT",
    "Sports"
};

// =====================================
// Utility Functions
// =====================================

bool isValidCampus(const string& campus) {

    return find(campuses.begin(),
                campuses.end(),
                campus) != campuses.end();
}

bool isValidDepartment(const string& department) {

    return find(validDepartments.begin(),
                validDepartments.end(),
                department) != validDepartments.end();
}

bool authenticateCampus(const string& campus,
                        const string& password) {

    for (size_t i = 0; i < campuses.size(); i++) {

        if (campuses[i] == campus &&
            passwords[i] == password) {

            return true;
        }
    }

    return false;
}

// =====================================
// Send TCP Message
// =====================================

bool sendMessageToSocket(int socket,
                         const string& message) {

    int sentBytes =
        send(socket,
             message.c_str(),
             message.size(),
             0);

    return sentBytes >= 0;
}

// =====================================
// Cleanup Client
// =====================================

void cleanupClient(const string& campus,
                   int clientSocket) {

    lock_guard<mutex> lock(clientMutex);

    if (!campus.empty()) {

        connectedClients.erase(campus);

        campusStatuses.erase(campus);

        cout << "[DISCONNECTED] "
             << campus
             << endl;
    }

    close(clientSocket);
}

// =====================================
// Handle Client
// =====================================

void handleClient(int clientSocket) {

    char buffer[BUFFER_SIZE];

    string currentCampus;

    bool authenticated = false;

    while (true) {

        memset(buffer, 0, BUFFER_SIZE);

        int receivedBytes =
            recv(clientSocket,
                 buffer,
                 BUFFER_SIZE - 1,
                 0);

        // =====================================
        // Client Disconnected
        // =====================================

        if (receivedBytes == 0) {

            cout << "[INFO] Client disconnected.\n";

            cleanupClient(currentCampus,
                          clientSocket);

            return;
        }

        // =====================================
        // recv() Failed
        // =====================================

        if (receivedBytes < 0) {

            cerr << "[ERROR] Failed to receive data.\n";

            cleanupClient(currentCampus,
                          clientSocket);

            return;
        }

        string message(buffer, receivedBytes);

        stringstream ss(message);

        string packetType;

        getline(ss, packetType, '|');

        // =====================================
        // LOGIN
        // =====================================

        if (packetType == "LOGIN") {

            if (authenticated) {

                sendMessageToSocket(
                    clientSocket,
                    "ERROR|ALREADY_AUTHENTICATED"
                );

                continue;
            }

            string campus;
            string password;

            getline(ss, campus, '|');
            getline(ss, password);

            // =========================
            // Validation
            // =========================

            if (campus.empty() ||
                password.empty()) {

                sendMessageToSocket(
                    clientSocket,
                    "AUTH|FAIL"
                );

                continue;
            }

            if (!isValidCampus(campus)) {

                sendMessageToSocket(
                    clientSocket,
                    "AUTH|FAIL"
                );

                continue;
            }

            // =========================
            // Authentication
            // =========================

            if (!authenticateCampus(campus,
                                    password)) {

                sendMessageToSocket(
                    clientSocket,
                    "AUTH|FAIL"
                );

                close(clientSocket);

                return;
            }

            // =========================
            // Duplicate Login Check
            // =========================

            {
                lock_guard<mutex> lock(clientMutex);

                if (connectedClients.count(campus)) {

                    sendMessageToSocket(
                        clientSocket,
                        "AUTH|ALREADY_CONNECTED"
                    );

                    close(clientSocket);

                    return;
                }

                connectedClients[campus] = {
                    clientSocket,
                    campus,
                    true
                };
            }

            authenticated = true;

            currentCampus = campus;

            cout << "[LOGIN SUCCESS] "
                 << campus
                 << endl;

            sendMessageToSocket(
                clientSocket,
                "AUTH|SUCCESS"
            );
        }

        // =====================================
        // MSG
        // =====================================

        else if (packetType == "MSG") {

            if (!authenticated) {

                sendMessageToSocket(
                    clientSocket,
                    "ERROR|NOT_AUTHENTICATED"
                );

                continue;
            }

            string targetCampus;
            string department;
            string text;

            getline(ss, targetCampus, '|');
            getline(ss, department, '|');
            getline(ss, text);

            // =========================
            // Validation
            // =========================

            if (targetCampus.empty() ||
                department.empty() ||
                text.empty()) {

                sendMessageToSocket(
                    clientSocket,
                    "ERROR|INVALID_MESSAGE_FORMAT"
                );

                continue;
            }

            if (!isValidCampus(targetCampus)) {

                sendMessageToSocket(
                    clientSocket,
                    "ERROR|INVALID_TARGET_CAMPUS"
                );

                continue;
            }

            if (!isValidDepartment(department)) {

                sendMessageToSocket(
                    clientSocket,
                    "ERROR|INVALID_DEPARTMENT"
                );

                continue;
            }

            // =========================
            // Route Message
            // =========================

            lock_guard<mutex> lock(clientMutex);

            if (!connectedClients.count(targetCampus)) {

                sendMessageToSocket(
                    clientSocket,
                    "ERROR|TARGET_OFFLINE"
                );

                continue;
            }

            string forwardedMessage =
                "FROM|" +
                currentCampus + "|" +
                department + "|" +
                text;

            bool success =
                sendMessageToSocket(
                    connectedClients[targetCampus].socket,
                    forwardedMessage
                );

            if (!success) {

                sendMessageToSocket(
                    clientSocket,
                    "ERROR|MESSAGE_DELIVERY_FAILED"
                );

                continue;
            }

            cout << "[ROUTED] "
                 << currentCampus
                 << " -> "
                 << targetCampus
                 << endl;
        }

        // =====================================
        // Invalid Packet
        // =====================================

        else {

            sendMessageToSocket(
                clientSocket,
                "ERROR|INVALID_PACKET"
            );
        }
    }
}

// =====================================
// UDP Listener
// =====================================

void udpListener() {

    int udpSocket =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (udpSocket < 0) {

        cerr << "[ERROR] Failed to create UDP socket.\n";

        return;
    }

    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(UDP_PORT);

    // =====================================
    // Bind UDP Socket
    // =====================================

    if (bind(udpSocket,
             (sockaddr*)&serverAddress,
             sizeof(serverAddress)) < 0) {

        cerr << "[ERROR] Failed to bind UDP socket.\n";

        close(udpSocket);

        return;
    }

    char buffer[BUFFER_SIZE];

    while (true) {

        sockaddr_in clientAddress{};

        socklen_t clientLength =
            sizeof(clientAddress);

        memset(buffer, 0, BUFFER_SIZE);

        int receivedBytes =
            recvfrom(udpSocket,
                     buffer,
                     BUFFER_SIZE - 1,
                     0,
                     (sockaddr*)&clientAddress,
                     &clientLength);

        if (receivedBytes < 0) {

            cerr << "[ERROR] Failed to receive UDP packet.\n";

            continue;
        }

        string message(buffer, receivedBytes);

        stringstream ss(message);

        string packetType;
        string campus;

        getline(ss, packetType, '|');
        getline(ss, campus);

        if (packetType != "HEARTBEAT") {

            cout << "[WARNING] Invalid UDP packet.\n";

            continue;
        }

        if (!isValidCampus(campus)) {

            cout << "[WARNING] Invalid campus in heartbeat.\n";

            continue;
        }

        lock_guard<mutex> lock(clientMutex);

        campusStatuses[campus] = {
            "ONLINE",
            clientAddress,
            time(nullptr)
        };

        cout << "[HEARTBEAT] "
             << campus
             << endl;
    }
}

// =====================================
// Display Connected Campuses
// =====================================

void showCampusStatuses() {

    lock_guard<mutex> lock(clientMutex);

    cout << "\n========== CONNECTED CAMPUSES ==========\n";

    if (campusStatuses.empty()) {

        cout << "No campuses online.\n";
    }

    for (const auto& entry : campusStatuses) {

        time_t currentTime = time(nullptr);

        double seconds =
            difftime(currentTime,
                     entry.second.lastSeen);

        string status =
            seconds <= 20
            ? "ONLINE"
            : "OFFLINE";

        cout << entry.first
             << " => "
             << status
             << endl;
    }

    cout << "========================================\n";
}

// =====================================
// Broadcast Announcement
// =====================================

void broadcastAnnouncement() {

    string announcement;

    cout << "Enter announcement: ";

    getline(cin, announcement);

    if (announcement.empty()) {

        cout << "[ERROR] Announcement cannot be empty.\n";

        return;
    }

    int udpSocket =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (udpSocket < 0) {

        cerr << "[ERROR] Failed to create UDP socket.\n";

        return;
    }

    string broadcastMessage =
        "BROADCAST|" + announcement;

    lock_guard<mutex> lock(clientMutex);

    for (const auto& entry : campusStatuses) {

        int sentBytes =
            sendto(udpSocket,
                   broadcastMessage.c_str(),
                   broadcastMessage.size(),
                   0,
                   (sockaddr*)&entry.second.udpAddress,
                   sizeof(entry.second.udpAddress));

        if (sentBytes < 0) {

            cerr << "[ERROR] Failed to send broadcast to "
                 << entry.first
                 << endl;
        }
    }

    close(udpSocket);

    cout << "[SUCCESS] Broadcast sent.\n";
}

// =====================================
// Admin Panel
// =====================================

void adminPanel() {

    while (true) {

        cout << "\n========== ADMIN PANEL ==========\n";
        cout << "1. View Campus Status\n";
        cout << "2. Broadcast Announcement\n";
        cout << "3. Exit\n";
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

                showCampusStatuses();

                break;

            case 2:

                broadcastAnnouncement();

                break;

            case 3:

                cout << "Server shutting down...\n";

                exit(0);

            default:

                cout << "[ERROR] Invalid menu choice.\n";
        }
    }
}

// =====================================
// Main
// =====================================

int main() {

    int serverSocket =
        socket(AF_INET,
               SOCK_STREAM,
               0);

    if (serverSocket < 0) {

        cerr << "[ERROR] Failed to create TCP socket.\n";

        return 1;
    }

    int opt = 1;

    setsockopt(serverSocket,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(TCP_PORT);

    // =====================================
    // Bind TCP Socket
    // =====================================

    if (bind(serverSocket,
             (sockaddr*)&serverAddress,
             sizeof(serverAddress)) < 0) {

        cerr << "[ERROR] Failed to bind TCP socket.\n";

        close(serverSocket);

        return 1;
    }

    // =====================================
    // Listen
    // =====================================

    if (listen(serverSocket,
               MAX_PENDING_CONNECTIONS) < 0) {

        cerr << "[ERROR] Failed to listen for connections.\n";

        close(serverSocket);

        return 1;
    }

    cout << "====================================\n";
    cout << "Central Server Running\n";
    cout << "TCP Port : " << TCP_PORT << endl;
    cout << "UDP Port : " << UDP_PORT << endl;
    cout << "====================================\n";

    thread udpThread(udpListener);

    thread adminThread(adminPanel);

    // =====================================
    // Accept Clients
    // =====================================

    while (true) {

        sockaddr_in clientAddress{};

        socklen_t clientLength =
            sizeof(clientAddress);

        int clientSocket =
            accept(serverSocket,
                   (sockaddr*)&clientAddress,
                   &clientLength);

        if (clientSocket < 0) {

            cerr << "[ERROR] Failed to accept client.\n";

            continue;
        }

        cout << "[NEW CONNECTION] Client connected.\n";

        thread clientThread(handleClient,
                            clientSocket);

        clientThread.detach();
    }

    udpThread.join();

    adminThread.join();

    close(serverSocket);

    return 0;
}