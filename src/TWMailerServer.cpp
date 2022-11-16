#include <ldap.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <experimental/filesystem>
#include <fstream> 
#include <dirent.h>

using namespace std;

///////////////////////////////////////////////////////////////////////////////

#define BUF 1024

///////////////////////////////////////////////////////////////////////////////

int abortRequested = 0;
int create_socket = -1;
int new_socket = -1;
string dirname;
string authenticatedUser;
int loginAttempts = 0;

///////////////////////////////////////////////////////////////////////////////

void* clientCommunication(void* data);
void signalHandler(int sig);
void* s_threading(void* arg);
int saveMessage(vector<string> msg);
vector<string> listFiles(char* directory);
void listMessages(int* current_socket);
void readMessage(vector<string> msg, int* socket);
void delMessage(vector<string> msg, int* socket);
int authenticateUser(vector<string> msg);
int ldapAuthentication(const char ldapBindPassword[], const char ldapUser[]);

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
    if (argc < 2) {
        cerr << "Missing arguments!" << endl;
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    dirname = argv[2];

    socklen_t addrlen;
    struct sockaddr_in address, cliaddress;
    int reuseValue = 1;

    ////////////////////////////////////////////////////////////////////////////
    // SIGNAL HANDLER
    // SIGINT (Interrup: ctrl+c)
    // https://man7.org/linux/man-pages/man2/signal.2.html
    if (signal(SIGINT, signalHandler) == SIG_ERR)
    {
        perror("signal can not be registered");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
   // CREATE A SOCKET
   // https://man7.org/linux/man-pages/man2/socket.2.html
   // https://man7.org/linux/man-pages/man7/ip.7.html
   // https://man7.org/linux/man-pages/man7/tcp.7.html
   // IPv4, TCP (connection oriented), IP (same as client)
    if ((create_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Socket error"); // errno set by socket()
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // SET SOCKET OPTIONS
    // https://man7.org/linux/man-pages/man2/setsockopt.2.html
    // https://man7.org/linux/man-pages/man7/socket.7.html
    // socket, level, optname, optvalue, optlen
    if (setsockopt(create_socket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuseValue,
        sizeof(reuseValue)) == -1)
    {
        perror("set socket options - reuseAddr");
        return EXIT_FAILURE;
    }

    if (setsockopt(create_socket,
        SOL_SOCKET,
        SO_REUSEPORT,
        &reuseValue,
        sizeof(reuseValue)) == -1)
    {
        perror("set socket options - reusePort");
        return EXIT_FAILURE;
    }


    ////////////////////////////////////////////////////////////////////////////
    // INIT ADDRESS
    // Attention: network byte order => big endian
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    ////////////////////////////////////////////////////////////////////////////
    // ASSIGN AN ADDRESS WITH PORT TO SOCKET
    if (bind(create_socket, (struct sockaddr*)&address, sizeof(address)) == -1)
    {
        perror("bind error");
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // ALLOW CONNECTION ESTABLISHING
    // Socket, Backlog (= count of waiting connections allowed)
    if (listen(create_socket, 5) == -1)
    {
        perror("listen error");
        return EXIT_FAILURE;
    }

    //new thread
    pthread_t thread;
    while (!abortRequested)
    {
        /////////////////////////////////////////////////////////////////////////
        // ignore errors here... because only information message
        // https://linux.die.net/man/3/printf
        printf("Waiting for connections...\n");

        /////////////////////////////////////////////////////////////////////////
        // ACCEPTS CONNECTION SETUP
        // blocking, might have an accept-error on ctrl+c
        addrlen = sizeof(struct sockaddr_in);
        if ((new_socket = accept(create_socket,
            (struct sockaddr*)&cliaddress,
            &addrlen)) == -1)
        {
            if (abortRequested)
            {
                perror("accept error after aborted");
            }
            else
            {
                perror("accept error");
            }
            break;
        }
        //create thread
        pthread_create(&thread, NULL, s_threading, (void*)&new_socket);
    }

    // frees the descriptor
    if (create_socket != -1)
    {
        if (shutdown(create_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown create_socket");
        }
        if (close(create_socket) == -1)
        {
            perror("close create_socket");
        }
        create_socket = -1;
    }

    return EXIT_SUCCESS;
}

void * s_threading(void* arg){      //create thread for each client
    cout << "A Client connected to the server!" << endl;
    int clientfd = *((int*)arg);
    pthread_detach(pthread_self());

    //client communication
    clientCommunication(&clientfd);
    close(clientfd);
    return nullptr;
}

void sendMessage(int* socket, const char* msg){     //send message to client
    if (send(*socket, msg, strlen(msg), 0) == -1)
    {
        perror("Message send failed!");
    }
}

void* clientCommunication(void* data)
{
    char buffer[BUF];
    int size;
    int* current_socket = (int*)data;

    ////////////////////////////////////////////////////////////////////////////
    // SEND welcome message
    strcpy(buffer, "Welcome to TWMailer!\r\nPlease enter one of the following commands:\r\n--> LOGIN \r\n--> SEND \r\n--> LIST \r\n--> READ (Type in the Subject instead of the Message-Number) \r\n--> DEL (Subject instead of Message-Number) \r\n--> QUIT \r\n");
    if (send(*current_socket, buffer, strlen(buffer), 0) == -1)
    {
        perror("send failed");
        return NULL;
    }

    do
    {
        /////////////////////////////////////////////////////////////////////////
        // RECEIVE
        size = recv(*current_socket, buffer, BUF - 1, 0);
        if (size == -1)
        {
            if (abortRequested)
            {
                perror("recv error after aborted");
            }
            else
            {
                perror("recv error");
            }
            break;
        }

        if (size == 0)
        {
            printf("Client closed remote socket\n"); // ignore error
            break;
        }

        // remove ugly debug message, because of the sent newline of client
        if (buffer[size - 2] == '\r' && buffer[size - 1] == '\n')
        {
            size -= 2;
        }
        else if (buffer[size - 1] == '\n')
        {
            --size;
        }

        buffer[size] = '\0';
        //printf("Message received: %s\n", buffer); // ignore error

        /*
        if (send(*current_socket, "OK", 3, 0) == -1)
        {
            perror("send answer failed");
            return NULL;
        }
        */
        vector<string> msg;
        string line = "";
        stringstream ss;
        ss << buffer << "\n";

        while (getline(ss, line))       //save msg in vector
        {
            msg.push_back(line);
        }

        if(authenticatedUser.empty()){
            if(msg[0] == "QUIT"){
                printf("Client closed connection\n");
                break;
            }

            if(msg[0] != "LOGIN"){
                printf("Unauthorized! Login first.");
                sendMessage(current_socket, "ERR");
            }else{
                if(authenticateUser(msg) == EXIT_FAILURE)
                    sendMessage(current_socket, "ERR");
                else
                    sendMessage(current_socket, "OK");
            }   
        }else{
            if(msg[0] =="SEND"){        //execute functions for each command
                if(saveMessage(msg) == -1)
                    sendMessage(current_socket, "ERR");
                else
                    sendMessage(current_socket, "OK");
            }else if(msg[0] =="LIST"){
                listMessages(current_socket);
            }else if(msg[0] =="READ"){
                readMessage(msg, current_socket);
            }else if(msg[0] =="DEL"){
                delMessage(msg, current_socket);
            }else if(msg[0] =="QUIT"){
                printf("Client closed connection\n");
            }else{
                sendMessage(current_socket, "Wrong Command, try again!");
            }
        }
        
    } while (strcmp(buffer, "quit") != 0 && !abortRequested);

    // closes/frees the descriptor if not already
    if (*current_socket != -1)
    {
        if (shutdown(*current_socket, SHUT_RDWR) == -1)
        {
            perror("shutdown new_socket");
        }
        if (close(*current_socket) == -1)
        {
            perror("close new_socket");
        }
        *current_socket = -1;
    }

    return NULL;
}

//Save sent message in given mail spool directory
int saveMessage(vector<string> msg){
    char cdir[256];
    getcwd(cdir, 256);
    char tmp_dir[256];
    getcwd(tmp_dir, 256);
    //strcat(tmp_dir, cdir);
    //change directory to given path
    chdir(dirname.c_str());

    //create new directory
    mkdir(msg[1].c_str(), 0777);

    //get current working directory
    getcwd(cdir, 256);
    
    //create path
    strcat(cdir, "/");
    strcat(cdir, msg[1].c_str());
    strcat(cdir, "/");

    chdir(cdir);

    //save message in new file
    ofstream newFile(msg[2] + ".txt");
    newFile << msg[1] << "\n" << msg[2] << "\n" << msg[3];
    newFile.close(); 
    chdir(tmp_dir);
    return 1;
}

vector<string> listFiles(char* directory){
    DIR *dir;
    struct dirent *file;
    vector<string> files;
    
    //get all filenames from given directory and copy them into vector
    if ((dir = opendir(directory)) != NULL) {
        while ((file = readdir(dir)) != NULL) {
            if(strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0)
                files.push_back(file->d_name);
        }
        closedir(dir);
    }else{
        cerr << "Error opening directory" << endl;
    }
    return files;
}

void listMessages(int* socket){
    char dir[256] = "";
    strcat(dir, dirname.c_str());
    strcat(dir, "/");
    strcat(dir, authenticatedUser.c_str());
    strcat(dir, "/");

    cout << dir << endl; 

    vector<string> files = listFiles(dir);      //get filenames
 
    string size = to_string(files.size()) += "\n";
    //send filenames to client
    sendMessage(socket, size.c_str());        
    for(string file : files){
        file += "\n";
        sendMessage(socket, file.c_str());
    }
}

void readMessage(vector<string> msg, int* socket){
    char dir[256] = "";
    char response[500] = "";
    DIR *directory;
    struct dirent *file;
    string fileText;
    strcat(dir, dirname.c_str());
    strcat(dir, "/");

    char tempDir[256]= "";
    getcwd(tempDir, 256);       //get current working directory, so after reading message, we can go back to base wd

    strcat(dir, authenticatedUser.c_str());
    strcat(dir, "/");

    msg[1] += ".txt";
    chdir(dir);
    getcwd(dir, 256);

    //open directory and read file content
    if ((directory = opendir(dir)) != NULL) { 
        while ((file = readdir(directory)) != NULL) {
            if(strcmp(file->d_name, msg[1].c_str()) == 0){      //compare filenames
                getcwd(dir, 256);
                ifstream newfile;
                newfile.open(file->d_name, ios::in);        //open file 

                if(newfile.is_open()){
                    strcat(response, "OK\n");
                    while(getline(newfile, fileText)){      //get content from file
                        fileText += "\n";
                        strcat(response, fileText.c_str());
                    }
                    sendMessage(socket, response);
                    newfile.close(); //close the file object
                }else{
                    cerr << "Error opening file" << endl;
                    sendMessage(socket, "ERR");
                }
                break;
            }
        }
        
        closedir(directory);
        
        chdir(tempDir);             //change back to working directory
        getcwd(tempDir, 256);

    }else{
        cerr << "Error opening directory" << endl;
        sendMessage(socket, "ERR");
    }
}

void delMessage(vector<string> msg, int* socket){
    char dir[256] = "";

    char tempDir[256]= "";
    getcwd(tempDir, 256);

    //change to desired directory
    strcat(dir, dirname.c_str());
    strcat(dir, "/");
    strcat(dir, authenticatedUser.c_str());
    strcat(dir, "/");
    chdir(dir);

    msg[1] += ".txt";
    
    //remove file from dir
    if(remove(msg[1].c_str()) == 0){
        sendMessage(socket, "OK");
    }else{
        sendMessage(socket, "ERR");
    }
    chdir(tempDir);
}

void signalHandler(int sig)
{
    if (sig == SIGINT)
    {
        printf("abort Requested... "); // ignore error
        abortRequested = 1;
        /////////////////////////////////////////////////////////////////////////
        // With shutdown() one can initiate normal TCP close sequence ignoring
        // the reference count.
        // https://beej.us/guide/bgnet/html/#close-and-shutdownget-outta-my-face
        // https://linux.die.net/man/3/shutdown
        if (new_socket != -1)
        {
            if (shutdown(new_socket, SHUT_RDWR) == -1)
            {
                perror("shutdown new_socket");
            }
            if (close(new_socket) == -1)
            {
                perror("close new_socket");
            }
            new_socket = -1;
        }

        if (create_socket != -1)
        {
            if (shutdown(create_socket, SHUT_RDWR) == -1)
            {
                perror("shutdown create_socket");
            }
            if (close(create_socket) == -1)
            {
                perror("close create_socket");
            }
            create_socket = -1;
        }
    }
    else
    {
        exit(sig);
    }
}

int authenticateUser(vector<string> msg){
    if(loginAttempts >= 3){
        printf("User gets blacklisted!");
        //blackListUser(); //must be implemented
        return -1;
    }
    loginAttempts++;

    if(msg.size() < 3){
        printf("Missing information!");
        return -1;
    }

    return ldapAuthentication(msg[2].c_str(), msg[1].c_str());
}

int ldapAuthentication(const char ldapBindPassword[], const char ldapUser[]){
    // LDAP config
    const char *ldapUri = "ldap://ldap.technikum-wien.at:389";
    const int ldapVersion = LDAP_VERSION3;

    //set username
    char ldapBindUser[256];
    sprintf(ldapBindUser, "uid=%s,ou=people,dc=technikum-wien,dc=at", ldapUser);
    printf("user set to: %s\n", ldapBindUser);

    // general
    int rc = 0; // return code

    ////////////////////////////////////////////////////////////////////////////
    // setup LDAP connection
    // https://linux.die.net/man/3/ldap_initialize
    LDAP *ldapHandle;
    rc = ldap_initialize(&ldapHandle, ldapUri);
    if (rc != LDAP_SUCCESS)
    {
        fprintf(stderr, "ldap_init failed\n");
        return -1;
    }
    printf("connected to LDAP server %s\n", ldapUri);

    ////////////////////////////////////////////////////////////////////////////
    // set verison options
    // https://linux.die.net/man/3/ldap_set_option
    rc = ldap_set_option(
        ldapHandle,
        LDAP_OPT_PROTOCOL_VERSION, // OPTION
        &ldapVersion);             // IN-Value
    if (rc != LDAP_OPT_SUCCESS)
    {
        // https://www.openldap.org/software/man.cgi?query=ldap_err2string&sektion=3&apropos=0&manpath=OpenLDAP+2.4-Release
        fprintf(stderr, "ldap_set_option(PROTOCOL_VERSION): %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // start connection secure (initialize TLS)
    // https://linux.die.net/man/3/ldap_start_tls_s
    // int ldap_start_tls_s(LDAP *ld,
    //                      LDAPControl **serverctrls,
    //                      LDAPControl **clientctrls);
    // https://linux.die.net/man/3/ldap
    // https://docs.oracle.com/cd/E19957-01/817-6707/controls.html
    //    The LDAPv3, as documented in RFC 2251 - Lightweight Directory Access
    //    Protocol (v3) (http://www.faqs.org/rfcs/rfc2251.html), allows clients
    //    and servers to use controls as a mechanism for extending an LDAP
    //    operation. A control is a way to specify additional information as
    //    part of a request and a response. For example, a client can send a
    //    control to a server as part of a search request to indicate that the
    //    server should sort the search results before sending the results back
    //    to the client.
    rc = ldap_start_tls_s(
        ldapHandle,
        NULL,
        NULL);
    if (rc != LDAP_SUCCESS)
    {
        fprintf(stderr, "ldap_start_tls_s(): %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
        return EXIT_FAILURE;
    }

    ////////////////////////////////////////////////////////////////////////////
    // bind credentials
    // https://linux.die.net/man/3/lber-types
    // SASL (Simple Authentication and Security Layer)
    // https://linux.die.net/man/3/ldap_sasl_bind_s
    // int ldap_sasl_bind_s(
    //       LDAP *ld,
    //       const char *dn,
    //       const char *mechanism,
    //       struct berval *cred,
    //       LDAPControl *sctrls[],
    //       LDAPControl *cctrls[],
    //       struct berval **servercredp);

    BerValue bindCredentials;
    bindCredentials.bv_val = (char *)ldapBindPassword;
    bindCredentials.bv_len = strlen(ldapBindPassword);
    BerValue *servercredp; // server's credentials
    rc = ldap_sasl_bind_s(
        ldapHandle,
        ldapBindUser,
        LDAP_SASL_SIMPLE,
        &bindCredentials,
        NULL,
        NULL,
        &servercredp);
    if (rc != LDAP_SUCCESS)
    {
        fprintf(stderr, "LDAP bind error: %s\n", ldap_err2string(rc));
        ldap_unbind_ext_s(ldapHandle, NULL, NULL);
        return EXIT_FAILURE;
    }

    authenticatedUser = ldapUser;
    return EXIT_SUCCESS;
}
