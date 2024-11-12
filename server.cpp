
// Linux Specific Headers.
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

// C++ Headers.
#include <cstdio>
#include <string>
#include <vector>
#include <regex>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>

// User defined headers.
#include "httpUtils.h"

const int MAX_CONNECTIONS = 100;
const int BUFFER_SIZE = 4096;

time_t parseHTTPDate(const std::string& httpDate)
{
    std::tm tm = {};
    std::stringstream ss(httpDate);

    ss >> std::get_time(&tm, "%a, %d %b %Y %H:%M:%S");
    if (ss.fail())
    {
        throw std::runtime_error("Failed to parse date!");
    }

    // De-select daylight savings for GMT.
    // System will determine automatically.
    tm.tm_isdst = -1;
    return timegm(&tm);

} // parseHTTPDate



http::retDateParse checkIfTimePassed(const std::string &httpTime)
{
    time_t checkTime;
    try
    {
        checkTime = parseHTTPDate(httpTime); 
    }
    catch (const std::runtime_error& e)
    {
        return http::retDateParse::FAILURE_TO_PARSE;
    }

    // Get the current time
    time_t currentTime = time(nullptr);

    if (checkTime <= currentTime)
    {
        // Given time has elapsed.
        return http::retDateParse::TIME_UNIT_PASSED;
    }

    // Time has not yet passed.
    return http::retDateParse::SUCCESS;

} // checkIfTimePassed



bool parseHTTPrequest(const std::string &getRequest, std::string &path, std::string &hostName)
{
    std::string pattern = R"(^GET\s+(\S+)\s+HTTP\/1\.0\r\nHost:\s*(\S+)\r\n)";
    std::regex regHTTP(pattern);
    std::smatch matches;

    bool retVal = std::regex_search(getRequest, matches, regHTTP);
    if (!retVal)
    {
        printf("Failed to parse GET request!\n");
        return false;
    }

    path = matches.str(1).c_str();
    hostName  = matches.str(2).c_str();

    return true;

} // parseHTTPrequest



void parseHTTPresponse(const std::string &httpResponse, std::string &date,
                       std::string lastModified, std::string & expires)
{
    std::string datePattern = R"(Date:\s*(.*)\r\n)";
    std::smatch match;
    std::regex regDate(datePattern);

    // Fetch Last accessed.
    if (std::regex_search(httpResponse, match, regDate))
    {
        date = match.str(1).c_str();
    }

    // Fetch 'Last Modified' field
    std::string lastModifiedPattern = R"(Last-Modified:\s*(.*)\r\n)";
    std::regex regLastModified(lastModifiedPattern);
    if (std::regex_search(httpResponse, match, regLastModified))
    {
        lastModified = match.str(1).c_str();
    }

    // Fetch 'Expires' field.
    std::string expiresPattern = R"(Expires:\s*(.*)\r\n)";
    std::regex regExpires(expiresPattern);
    if (std::regex_search(httpResponse, match, regExpires))
    {
        expires = match.str(1).c_str();
    }

} // parseHTTPresponse



// Method to parse the body from a Web Page.
std::string parseHTTPBody(const std::string &httpResponse)
{
    size_t endOfHeader = httpResponse.find("\r\n\r\n");
    if (endOfHeader == std::string::npos)
    {
        return {};
    }

    // Body starts 4 chars after the postition.
    // The de-limiter between Header and Body is ==> "\r\n\r\n"
    std::string httpBody;
    try
    {
        httpBody = httpResponse.substr(endOfHeader + 4);
    }
    catch (const std::out_of_range& e)
    {
        printf("Failed to fetch Body from Web Page. Error = {%s}\n", e.what());
        return {};
    }

    return httpBody;

} // parseHTTPBody



// Method to connect to a web server using a hostname and port
int connectToServer(const std::string& hostName, const std::string& port = "80")
{
    // ref: Beej's Guide.
    struct addrinfo hints, *server_info, *p;
    int sockFd;

    // Set up hints for getaddrinfo to specify the kind of connection we want
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    // Get address information for the specified hostname and port
    int status = getaddrinfo(hostName.c_str(), port.c_str(), &hints, &server_info);
    if (status != 0)
    {
        printf("Failed to resolve hostname! Error = {%s}\n", gai_strerror(status));
        return -1;
    }

    // Try to connect to one of the returned addresses
    for (p = server_info; p != NULL; p = p->ai_next)
    {
        // Create a socket
        sockFd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockFd == -1)
        {
            // Try the next address if socket creation failed
            continue;
        }

        // Try to connect to the server
        if (connect(sockFd, p->ai_addr, p->ai_addrlen) == -1)
        {
            close(sockFd);
            // Try the next address if connection failed
            continue;
        }

        // If connection succeeds, stop looping
        break;
    }

    // Free the address info structure
    freeaddrinfo(server_info);

    // Check if we failed to connect
    if (p == NULL)
    {
        printf("Failed to connect to: {%s}\n", hostName.c_str());
        return -1;
    }

    // Return the connected socket descriptor
    return sockFd;

} // connectToServer



// Method to to send and retrive data from the 'If-Modified-Since' request.
std::string sendIfModifiedSinceRequest(const std::string &date, const http::httpHeader &header)
{
    char ifModReq[256];
    char buffer[BUFFER_SIZE];
    std::string responseBuffer;
    ssize_t bytesReceived;

    int newSockFd = connectToServer(header.hostName);
    if (newSockFd == -1)
    {
        printf("Failed to connect establish connection to server!\n");
        return {};
    }
    snprintf(ifModReq, sizeof(ifModReq), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\nIf-Modified-Since: %s\r\n\r\n",
             header.filePath.c_str(), header.hostName.c_str(), date.c_str());
    
    printf("\nSending 'If-Modified-Since' request\n");
    // Send new request.
    if (send(newSockFd, ifModReq, sizeof(ifModReq), 0) == -1)
    {
        printf("Failed to send data!\n");
        return {};
    }

    while ((bytesReceived = recv(newSockFd, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytesReceived] = '\0';
        responseBuffer += std::string(buffer);
    }

    return responseBuffer;

} // sendIfModifiedSinceRequest



// Method to add and remove elements from the LRU cache.
http::httpHeader handleCache(const http::httpHeader &header, http::LRUCache *cache)
{
    std::string key = header.hostName + header.filePath;
    http::httpHeader fetchedHeader;
    try
    {
        fetchedHeader = cache->fetch(key);
    }
    catch (const std::runtime_error &e)
    {
        printf("Cache Miss!\n");
        printf("%s. Adding Web Page to cache.\n", e.what());
        cache->add(key, header);
        return header;
    }

    printf("\n\nCache hit! Further processing required to determine validity of Web Page.\n\n");

    // Element is in cache. So we need to check if the
    // data is stale or not. Loop over each of the dates to
    // check if we get an apppropriate match.
    int priority = 1;
    bool exitLoop = false;
    std::string requestDate;
    while (priority <= 3)
    {
        switch (priority)
        {
            case 1:
                if (checkIfTimePassed(header.expires) == http::retDateParse::SUCCESS)
                {
                    requestDate = header.expires;
                    exitLoop = true;
                }
                break;

            case 2:
                // In case 2, we only need to check whether the parsing has been successful.
                // We need check whether the time has elapsed. That only applies for the
                // 'Expires' header.
                if (checkIfTimePassed(header.lastModified) != http::retDateParse::FAILURE_TO_PARSE)
                {
                    requestDate = header.lastModified;
                    exitLoop = true;
                }
                break;

            case 3:
                // Follow same logic in Case 3 as in Case 2.
                if (checkIfTimePassed(header.lastAccessTime) != http::retDateParse::FAILURE_TO_PARSE)
                {
                    requestDate = header.lastAccessTime;
                    exitLoop = true;
                }
                break;

            default:
                // do nothing.
                break;
        }

        if (exitLoop)
        {
            break;
        }

        priority++;
    }

    if (!exitLoop)
    {
        printf("Failed to verify whether Web Page has been modified.\n");
        printf("Warning: Page may have been modified since last access. Use at your own discretion!\n");
        return fetchedHeader;
    }

    // If execution reaches this point, we need to
    // send an If Modified request to the server.
    std::string newResponse = sendIfModifiedSinceRequest(requestDate, fetchedHeader);
    if (newResponse.empty())
    {
        printf("Failed to retrieve response after sending 'If-Modified-Since' request.\n");
        printf("Serving outdated Web page present in cache...\n");
        printf("Warning: Page may have been modified since last access. Use at your own discretion!\n");
        return fetchedHeader;
    }

    std::string date, lastModified, expires;
    http::httpHeader updatedHeader;
    parseHTTPresponse(newResponse, date, lastModified, expires);

    newResponse = parseHTTPBody(newResponse);
    if (newResponse.empty())
    {
        printf("Cannot serve Web page to client. Failure to ascertain body of the Web Page.\n");
        printf("Serving outdated Web page present in cache...\n");
        printf("Warning: Page may have been modified since last access. Use at your own discretion!\n");
        return fetchedHeader;
    }

    updatedHeader.hostName = fetchedHeader.hostName;
    updatedHeader.filePath = fetchedHeader.filePath;
    updatedHeader.lastAccessTime = date;
    updatedHeader.lastModified = lastModified;
    updatedHeader.expires = expires;
    updatedHeader.body = newResponse;

    // Add this new data to cache.
    cache->add(key, updatedHeader);

    return updatedHeader;

} // handleCache



int main(int argc, char *argv[])
{
    int serverSocketFd;
    struct sockaddr_in serverAddr, clientAddr;

    // Some pre-flight checks.
    if (argc > 3)
    {
        printf("Only 3 arguments allowed! Please try again...\n");
        exit(EXIT_FAILURE);
    }
    else if ((argc == 1) || (argc == 2))
    {
        printf("Too few arguments! Please enter the IP address and port number to bind to.\n");
        exit(EXIT_FAILURE);
    }

    // Fetch the port as argument.
    char *portToConnect = argv[2];

    // Create a socket descriptor for an IPV4 address and handling TCP connections.
    serverSocketFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocketFd == -1)
    {
        printf("Failed to create socket! serverSocketFd = [%d] Error = {%s}\n", serverSocketFd, strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("Socket created successfully.\n");

    // Initialize the server struct with the necessary data.
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(atoi(portToConnect));
    char *ipToBind = argv[1];
    serverAddr.sin_addr.s_addr = inet_addr(ipToBind);

    // Bind to the socket descriptor created earlier.
    int retVal = bind(serverSocketFd, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    // Exit if binding failed.
    if (retVal == -1)
    {
        printf("Failed to bind socket! retVal = [%d] Error = {%s}\n", retVal, strerror(errno));
        close(serverSocketFd);
        exit(EXIT_FAILURE);
    }
    printf("Socket binding successfull.\n");

    // Listen for connections. We can handle upto 100 connections simlutaneously.
    // 100 should be good for now but change this value and recompile if required.
    retVal = listen(serverSocketFd, MAX_CONNECTIONS);
    // Exit if listening failed.
    if (retVal == -1)
    {
        printf("Failed to listen for incoming connections! retVal = [%d] Error = {%s}\n", retVal, strerror(errno));
        close(serverSocketFd);
        exit(EXIT_FAILURE);
    }
    printf("Listening for incoming connections...\n\n\n");


    int MaxFd;
    fd_set readFds, masterReadFds;
    FD_ZERO(&readFds);
    FD_ZERO(&masterReadFds);

    // fd_set writeFds, masterWriteFds;
    // FD_ZERO(&writeFds);
    // FD_ZERO(&masterWriteFds);

    FD_SET(serverSocketFd, &masterReadFds);
    MaxFd = serverSocketFd;

    char receiveBuf[BUFFER_SIZE];

    // Set Max capacity of the cache.
    http::LRUCache *cache = new http::LRUCache(10);

    while (true)
    {
        // Take a copy of the master list of file descriptors.
        // This is because the file descriptors passed to select is cleared
        // after processing.
        readFds = masterReadFds;
        // printf("MAX File Descriptor is {%d}\n", MaxFd);

        int retVal = select(MaxFd + 1, &readFds, NULL, NULL, NULL);
        if (retVal == -1)
        {
            printf("Failed to multiplex on the Client desciptors! retVal = (%d) Error = {%s}\n",
                    retVal, strerror(errno));
            exit(EXIT_FAILURE);
        }

        for (int descriptor = 0; descriptor <= MaxFd; descriptor++)
        {
            if (FD_ISSET(descriptor, &readFds) == 0)
            {
                continue;
            }

            // We have a File descriptor ready for reading.

            if (descriptor == serverSocketFd)
            {
                printf("Got a new connection!\n");
                socklen_t clientAddrLen = sizeof(clientAddr);
                int clientSocketFd = accept(serverSocketFd, (struct sockaddr*)&clientAddr, &clientAddrLen);
                if (clientSocketFd == -1)
                {
                    printf("Failed to establish a connection with the client! clientSocketFd = [%d] Error = {%s}\n",
                           clientSocketFd, strerror(errno));
                    continue;
                }

                printf("Successfully established a connection with the client!\n\n");
                printf("Waiting for data...\n");

                // Add new client FD to the master list.
                FD_SET(clientSocketFd, &masterReadFds);
                if (clientSocketFd > MaxFd)
                {
                    // Set new maximum based on the above
                    // condition.
                    MaxFd = clientSocketFd;
                }

                continue;
            }

            printf("Received data!\n");
            memset(receiveBuf, 0, BUFFER_SIZE);
            int recBytes = read(descriptor, receiveBuf, BUFFER_SIZE);
            // Failed to read data and hence exit.
            if (recBytes == -1)
            {
                printf("Failed to read data from socket! Error = {%s}\n", strerror(errno));
                FD_CLR(descriptor, &masterReadFds);
                close(descriptor);
                continue;
            }

            std::string getRequest = std::string(receiveBuf);
            std::string path, host;
            parseHTTPrequest(getRequest, path, host);

            // now open a new tcp connection.

            int webSockFd = connectToServer(host);
            if (webSockFd == -1)
            {
                printf("Failed to connect establish connection to server!\n");
                FD_CLR(descriptor, &masterReadFds);
                close(descriptor);
                continue;
            }

            printf("Connected to {%s}\n", host.c_str());

            // Send a simple HTTP GET request
            char httpRequest[512];
            snprintf(httpRequest, sizeof(httpRequest), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path.c_str(), host.c_str());
            send(webSockFd, httpRequest, sizeof(httpRequest), 0);

            // Read the response
            char buffer[4096];
            ssize_t bytesReceived;
            std::string responseBuffer;
            while ((bytesReceived = recv(webSockFd, buffer, sizeof(buffer) - 1, 0)) > 0)
            {
                buffer[bytesReceived] = '\0';
                responseBuffer += std::string(buffer);
            }

            // Close the connection explicitly.
            // Anyway the server terminates after it is done
            // serving the web page.
            close(webSockFd);

            std::string date, lastModified, expires;
            parseHTTPresponse(responseBuffer, date, lastModified, expires);

            responseBuffer = parseHTTPBody(responseBuffer);
            if (responseBuffer.empty())
            {
                printf("Cannot serve Web Page to the client. Failure to ascertain body of the webpage.\n");
                FD_CLR(descriptor, &masterReadFds);
                close(descriptor);
                continue;
            }

            // Form the header.
            http::httpHeader header;
            header.hostName = host;
            header.filePath = path;
            header.lastAccessTime = date;
            header.lastModified = lastModified;
            header.expires = expires;
            header.body = responseBuffer;

            http::httpHeader headerToReturn = handleCache(header, cache);

            std::string bufferToSend = headerToReturn.body;
            if (send(descriptor, bufferToSend.c_str(), bufferToSend.length(), 0) == -1)
            {
                printf("Failed to serve Web Page to client!\n");
            }

            printf("\nDone serving client\n");

            printf("\n-----------------------------------\n");

            FD_CLR(descriptor, &masterReadFds);
            close(descriptor);
        }
    }

    close(serverSocketFd);

    return 0;
}
