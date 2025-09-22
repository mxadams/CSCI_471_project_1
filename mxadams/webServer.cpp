// *****************************************************************
// * webServer (webServer.cpp)
// * - Implements a very limited subset of HTTP/1.0, use -v to enable verbose
// debugging output.
// * - Port number 1701 is the default, if in use random number is selected.
// *
// * - GET requests are processed, all other metods result in 400.
// *     All header gracefully ignored
// *     Files will only be served from cwd and must have format file\d.html or
// image\d.jpg
// *
// * - Response to a valid get for a legal filename
// *     status line (i.e., response method)
// *     Cotent-Length:
// *     Content-Type:
// *     \r\n
// *     requested file.
// *
// * - Response to a GET that contains a filename that does not exist or is not
// allowed
// *     statu line w/code 404 (not found)
// *
// * - CSCI 471 - All other requests return 400
// * - CSCI 598 - HEAD and POST must also be processed.
// *
// * - Program is terminated with SIGINT (ctrl-C)
// *****************************************************************
#include "webServer.h"

// global quit received flag variable
volatile sig_atomic_t quitProgram = 0;

// *****************************************************************
// * Signal Handler.
// * - Display the signal and exit (returning 0 to OS indicating normal
// shutdown)
// * - Optional for 471, required for 598
// *****************************************************************
void sig_handler(int signo) {
  if (signo == SIGINT) {
    INFO << "Caught SIGINT, shutting down." << ENDL;
    quitProgram = 1;
  }
}

// *****************************************************************
// * processRequest,
//   - Return HTTP code to be sent back
//   - Set filename if appropriate. Filename syntax is valided but existance is
//   not verified.
// *****************************************************************
int readHeader(int sockFd, std::string &filename) {
  std::string request;
  char buffer[BUFFER_SIZE];
  bool headerComplete = false;
  // loop through and read header
  while (!headerComplete && !quitProgram) {
    bzero(buffer, BUFFER_SIZE);
    ssize_t bytesRead = read(sockFd, buffer, BUFFER_SIZE - 1);
    if (bytesRead < 0) {
      ERROR << "Error reading from socket: " << strerror(errno) << ENDL;
      return 400;
    } else if (bytesRead == 0) {
      DEBUG << "Client closed connection before sending complete header"
            << ENDL;
      return 400;
    }
    request.append(buffer, bytesRead);
    size_t headerEnd = request.find("\r\n\r\n");
    if (headerEnd != std::string::npos) {
      headerComplete = true;
      request = request.substr(0, headerEnd);
    }
  }
  DEBUG << "Received request header:\n" << request << ENDL;
  // check request
  if (request.substr(0, 3) != "GET") {
    DEBUG << "Not a GET request" << ENDL;
    return 400;
  }
  size_t start = request.find(' ') + 1;
  size_t end = request.find(' ', start);
  if (start == std::string::npos || end == std::string::npos) {
    DEBUG << "Malformed request" << ENDL;
    return 400;
  }
  // get requested filename
  filename = request.substr(start, end - start);
  if (!filename.empty() && filename[0] == '/') {
    filename = filename.substr(1);
  }
  DEBUG << "Extracted filename: " << filename << ENDL;
  // validate filename using regex
  std::regex validFilePattern("(file[0-9]\\.html)|(image[0-9]\\.jpg)");
  if (!std::regex_match(filename, validFilePattern)) {
    DEBUG << "Invalid filename" << ENDL;
    return 404;
  }
  std::string filepath = "data/" + filename;
  struct stat fileStat;
  if (stat(filepath.c_str(), &fileStat) < 0) {
    DEBUG << "File does not exist: " << filepath << ENDL;
    return 404;
  }
  DEBUG << "Valid GET request for file: " << filename << ENDL;
  return 200;
}

// *****************************************************************
// * Send one line (including the line terminator <LF><CR>)
// * - Assumes the terminator is not included, so it is appended.
// *****************************************************************
void sendLine(int socketFd, std::string &stringToSend) {
  std::string line = stringToSend + "\r\n";
  write(socketFd, line.c_str(), line.length());
}

// *****************************************************************
// * Send the entire 404 response, header and body.
// *****************************************************************
void send404(int sockFd) {
  std::string response;
  response = "HTTP/1.0 404 Not Found";
  sendLine(sockFd, response);
  response = "Content-Type: text/html";
  sendLine(sockFd, response);
  response = "";
  sendLine(sockFd, response);
  response = "<html><body><h1>404 Not Found</h1><p>The requested file was not "
             "found on this server.</p></body></html>";
  sendLine(sockFd, response);
}

// *****************************************************************
// * Send the entire 400 response, header and body.
// *****************************************************************
void send400(int sockFd) {
  std::string response;
  response = "HTTP/1.0 400 Bad Request";
  sendLine(sockFd, response);
  response = "Content-Type: text/html";
  sendLine(sockFd, response);
  response = "";
  sendLine(sockFd, response);
  response = "<html><body><h1>400 Bad Request</h1><p>Your browser sent a "
             "request that this server could not understand.</p></body></html>";
  sendLine(sockFd, response);
}

// *****************************************************************
// * sendFile
// * -- Send a file back to the browser.
// *****************************************************************
void sendFile(int sockFd, std::string filename) {
  std::string filepath = "data/" + filename;
  std::string response;
  struct stat fileStat;
  if (stat(filepath.c_str(), &fileStat) < 0) {
    ERROR << "File dissapeared after validation: " << filepath << " - "
          << strerror(errno) << ENDL;
    send404(sockFd);
    return;
  }
  // check file type
  std::string contentType;
  if (filename.find(".html") != std::string::npos) {
    contentType = "text/html";
  } else if (filename.find(".jpg") != std::string::npos) {
    contentType = "image/jpeg";
  } else {
    contentType = "application/octet-stream";
  }
  // send header
  response = "HTTP/1.0 200 OK";
  sendLine(sockFd, response);
  response = "Content-Type: " + contentType;
  sendLine(sockFd, response);
  response = "Content-Length: " + std::to_string(fileStat.st_size);
  sendLine(sockFd, response);
  response = "";
  sendLine(sockFd, response);
  // send file contents
  int fileFd = open(filepath.c_str(), O_RDONLY);
  if (fileFd < 0) {
    ERROR << "Failed to open file: " << filepath << " - " << strerror(errno)
          << ENDL;
    return;
  }
  char buffer[BUFFER_SIZE];
  ssize_t bytesRead;
  while ((bytesRead = read(fileFd, buffer, BUFFER_SIZE)) > 0) {
    write(sockFd, buffer, bytesRead);
  }
  close(fileFd);
  DEBUG << "File sent successfully: " << filename << ENDL;
}

// *****************************************************************
// * processConnection
// * -- process one connection/request.
// *****************************************************************
int processConnection(int sockFd) {
  std::string filename;
  int statusCode = readHeader(sockFd, filename);
  switch (statusCode) {
  case 200:
    sendFile(sockFd, filename);
    break;
  case 400:
    send400(sockFd);
    break;
  case 404:
    send404(sockFd);
    break;
  default:
    ERROR << "Unexpected status code: " << statusCode << ENDL;
    send400(sockFd);
    break;
  }
  return 0;
}

int main(int argc, char *argv[]) {

  // ***************************************************************
  // * Process the command line arguments
  // ***************************************************************
  int opt = 0;
  while ((opt = getopt(argc, argv, "d:")) != -1) {
    switch (opt) {
    case 'd':
      LOG_LEVEL = std::stoi(optarg);
      break;
    case ':':
    case '?':
    default:
      std::cout << "useage: " << argv[0] << " -d LOG_LEVEL" << std::endl;
      exit(-1);
    }
  }

  // ***************************************************************
  // * Catch all possible signals
  // ***************************************************************
  DEBUG << "Setting up signal handlers" << ENDL;
  if (signal(SIGINT, sig_handler) == SIG_ERR) {
    ERROR << "Can't catch SIGINT" << ENDL;
  }

  // ***************************************************************
  // * Creating the inital socket using the socket() call.
  // ***************************************************************
  int listenFd = socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd < 0) {
    FATAL << "Failed to create socket: " << strerror(errno) << ENDL;
    exit(-1);
  }
  DEBUG << "Calling Socket() assigned file descriptor " << listenFd << ENDL;

  // ***************************************************************
  // * The bind() call takes a structure used to spefiy the details of the
  // connection.
  // *
  // * struct sockaddr_in servaddr;
  // *
  // On a cient it contains the address of the server to connect to.
  // On the server it specifies which IP address and port to lisen for
  // connections. If you want to listen for connections on any IP address you
  // use the address INADDR_ANY
  // ***************************************************************
  struct sockaddr_in servaddr;
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  // ***************************************************************
  // * Binding configures the socket with the parameters we have
  // * specified in the servaddr structure.  This step is implicit in
  // * the connect() call, but must be explicitly listed for servers.
  // *
  // * Don't forget to check to see if bind() fails because the port
  // * you picked is in use, and if the port is in use, pick a different one.
  // ***************************************************************
  uint16_t port = 1701;
  DEBUG << "Calling bind()" << ENDL;
  bool bindSuccess = false;
  while (!bindSuccess) {
    servaddr.sin_port = htons(port);
    if (bind(listenFd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
      if (errno == EADDRINUSE) {
        WARNING << "Port " << port << " in use, trying next port" << ENDL;
        port++;
      } else {
        FATAL << "Bind failed: " << strerror(errno) << ENDL;
        exit(-1);
      }
    } else {
      bindSuccess = true;
    }
  }
  std::cout << "Using port: " << port << std::endl;

  // ***************************************************************
  // * Setting the socket to the listening state is the second step
  // * needed to being accepting connections.  This creates a que for
  // * connections and starts the kernel listening for connections.
  // ***************************************************************
  DEBUG << "Calling listen()" << ENDL;
  int listenq = 1;
  if (listen(listenFd, listenq) < 0) {
    FATAL << "Listen failed: " << strerror(errno) << ENDL;
    exit(-1);
  }

  // ***************************************************************
  // * The accept call will sleep, waiting for a connection.  When
  // * a connection request comes in the accept() call creates a NEW
  // * socket with a new fd that will be used for the communication.
  // ***************************************************************
  while (!quitProgram) {
    DEBUG << "Calling connFd = accept(fd,NULL,NULL)." << ENDL;
    int connFd = accept(listenFd, NULL, NULL);
    if (connFd < 0) {
      ERROR << "Accept failed: " << strerror(errno) << ENDL;
      continue;
    }
    DEBUG << "We have recieved a connection on " << connFd
          << ". Calling processConnection(" << connFd << ")" << ENDL;
    quitProgram = processConnection(connFd);
    DEBUG << "processConnection returned " << quitProgram
          << " (should always be 0)" << ENDL;
    DEBUG << "Closing file descriptor " << connFd << ENDL;
    close(connFd);
  }
  close(listenFd);
  ERROR << "Program fell through to the end of main. A listening socket may "
           "have closed unexpectadly."
        << ENDL;
  closefrom(3);
}