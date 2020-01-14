#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <string>
#include <thread>
#include "Server.h"

Server::~Server()
{
    Stop();
    deleteLock->lock();
    deleteLock->unlock();
    delete deleteLock;
    delete fdLock;
}

bool Server::Start(int port)
{
    deleteLock = new std::mutex();
    fdLock = new std::mutex();
    // Create a socket
    listening = socket(AF_INET, SOCK_STREAM, 0);
    if(listening == -1)
    {
        std::cerr << "Failed to create socket" << std::endl;;
        return false;
    }

    // Bind the socket to an ip and port
    hint.sin_family = AF_INET;
    hint.sin_port = htons(port);
    inet_pton(AF_INET, "0.0.0.0", &hint.sin_addr);


    if(bind(listening, (sockaddr*)&hint, sizeof(hint)) == -1)
    {
        std::cerr << "Failed to bind socket to IP or Port" << std::endl;
        std::cout << gai_strerror(errno) << std::endl;
        return false;
    }

    // Set the socket as a listening
    if(listen(listening, SOMAXCONN) == -1)
    {
        std::cerr << "Socket failed to start listening" << std::endl;;
    }

    FD_ZERO(&master);

    FD_SET(listening, &master);

    std::thread tr(&Server::ProcessNetworkEvents, this);

    tr.detach();

    std::cout << "Server Initialized!"  << std::endl;
    return true;
}

void Server::Stop()
{
    running = false;
    deleteLock->lock();
    deleteLock->unlock();
    close(listening);
    for(auto const& socket : sockets)
    {
        close(socket);
    }
    sockets.clear();
}

void Server::ProcessNetworkEvents()
{
    deleteLock->lock();
    running = true;

    while(running) {
        fdLock->lock();
        fd_set mCopy = master;
        fdLock->unlock();
        unsigned int socketCount = select(SOMAXCONN + 1, &mCopy, nullptr, nullptr, nullptr);


        if (FD_ISSET(listening, &mCopy))
        {
            HandleConnectionEvent();
        }
        else
        {
            fdLock.lock();
            for (auto const &socket : sockets)
            {
                if (FD_ISSET(socket, &mCopy))
                {
                    HandleMessageEvent(socket);
                    break;
                }
            }
        }
        fdLock.unlock();
    }

    deleteLock->unlock();
}

void Server::HandleConnectionEvent()
{
    sockaddr_in client;
    client.sin_family = AF_INET;
    socklen_t clientSize;
    char host[NI_MAXHOST];
    char svc[NI_MAXSERV];

    int clientSocket =
            accept(listening, (sockaddr*)&client, &clientSize);
    if(clientSocket == -1)
    {
        std::cerr << "Client failed to connect" << std::endl;;
    }

    fdLock->lock();
    // Add new connection to master set
    FD_SET(clientSocket, &master);
    sockets.push_front(clientSocket);
    fdLock->unlock();

    // Assign the connection a uid
    ClientInfo newClient;
    // Since unix sockets are represented as an int, we can just use those directly for the uid
    newClient.uid = clientSocket;

    memset(host, 0, NI_MAXHOST);
    memset(svc, 0, NI_MAXSERV);

    int result = getnameinfo((sockaddr*)&client,
                             sizeof(client),
                             host,
                             NI_MAXHOST,
                             svc,
                             NI_MAXSERV,
                             0);

    if(result == 0)
    {
        newClient.name = svc;
        newClient.ipv4 = host;

    }
    else
    {
        std::cout << gai_strerror(result) << std::endl;
        inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);

        newClient.name = host;
        newClient.ipv4 = ntohs(client.sin_port);
    }

    std::cout << "New Client: " << newClient.name << " Connected with ip " << newClient.ipv4 << std::endl;
    processNewClient(newClient);
}

void Server::HandleMessageEvent(int sock) {
    memset(cBuf, 0, MAX_PACKET_SIZE);

    int bytesReceived = recv(sock, cBuf, MAX_PACKET_SIZE, 0);
    if (bytesReceived <= 0)
    {
        close(sock);
        FD_CLR(sock, &master);
        sockets.remove(sock);
        std::cout << "Client disconnected" << std::endl;
        processDisconnectedClient(sock);
    }
    else
    {
        auto packet = new DataPacket();
        packet->data = new char[bytesReceived];
        memcpy(packet->data, cBuf, bytesReceived);
        packet->dataLength = bytesReceived;
        packet->senderId = sock;
        processPacket(packet);
    }
}

void Server::SendMessageToClient(const char *data, unsigned int dataLength, unsigned int client)
{
    if(dataLength > 0)
    {
        send(client, data, dataLength, 0);
    }
}